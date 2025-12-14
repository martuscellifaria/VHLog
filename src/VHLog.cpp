#include "VHLog.h"
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <mutex>
#include <format>
#include <print>
#include <string>

#ifdef USE_ASIO
VHLogger::VHLogger(bool debugEnvironment, std::size_t batchSize) : 
    socket_(ioContext_),
    basePathAndName_("") {

    workerRunning_ = true;
    batchSize_ = batchSize;
    tcpIsSending_ = false;
    unflushedBytes_ = 0;
    debugEnvironment_ = debugEnvironment;
    sinkTypes_.clear();
    socketConnected_ = false;
    shutdownSocket_.store(false, std::memory_order_release);  
    reconnectTimer_ = std::make_unique<asio::steady_timer>(ioContext_);
    vhlogShutdown_ = false;

    workGuard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(ioContext_)
    );
    loggerThread_ = std::thread(&VHLogger::loggerWorker, this);
    ioThread_ = std::thread([this] { 
        ioContext_.run(); 
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void VHLogger::shutdown() {
    
    workerRunning_ = false;
    condVar_.notify_all();
    
    if (loggerThread_.joinable()) {
        loggerThread_.join();
    }
    
    std::deque<std::pair<VHLogLevel, std::string>> remainingMessages;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        remainingMessages.swap(logMessageQueue_);
    }
    
    for (auto& [level, message] : remainingMessages) {
        if (!message.empty()) {
            writeToDestination(level, message);
        }
    }
    
    shutdownSocket_.store(true, std::memory_order_release);
    if (reconnectTimer_) {
        reconnectTimer_->cancel();
    }
    
    std::error_code ec;
    socket_.cancel(ec);
    
    ioContext_.restart();
    while (ioContext_.poll_one() > 0) {}
    
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        tcpMessageQueue_.clear();
        tcpIsSending_ = false;
    }
    
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        if (socket_.is_open()) {
            socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
            socket_.close(ec);
        }
        socketConnected_ = false;
    }
    
    ioContext_.stop();
    
    if (workGuard_) {
        workGuard_.reset();
    }
    
    if (ioThread_.joinable()) { 
        ioThread_.join();
    }
    
    ioContext_.restart();
    while (ioContext_.poll_one() > 0) {}
    
    if (file_ && file_.is_open()) {
        file_.flush();
        file_.close();
    }
    
    vhlogShutdown_ = true;
}

#else
VHLogger::VHLogger(bool debugEnvironment, std::size_t batchSize) : 
    basePathAndName_("") {
    workerRunning_ = true;
    batchSize_ = batchSize;
    unflushedBytes_ = 0;
    debugEnvironment_ = debugEnvironment;
    sinkTypes_.clear();
    loggerThread_ = std::thread(&VHLogger::loggerWorker, this);
}

void VHLogger::shutdown() {

    workerRunning_ = false;
    
    condVar_.notify_all();
    
    if (loggerThread_.joinable()) {
        loggerThread_.join();
    }

    if (file_ && file_.is_open()) {
        file_.flush();
        file_.close();
    }
    vhlogShutdown_ = true;
}

#endif

VHLogger::~VHLogger() {

    if (!vhlogShutdown_) {
        shutdown();
    }
}

void VHLogger::loggerWorker() {

    std::vector<std::pair<VHLogLevel, std::string>> batch;
    
    while (true) {
        std::unique_lock<std::mutex> lock(queueMutex_);
        
        condVar_.wait(lock, [this]() {
            return !logMessageQueue_.empty() || !workerRunning_;
        });
        
        if (!workerRunning_ && logMessageQueue_.empty()) {
            break;
        }
        
        const size_t available = logMessageQueue_.size();
        const size_t batch_size = std::min(available, static_cast<size_t>(batchSize_));
        
        batch.reserve(batch_size);
        
        for (size_t i = 0; i < batch_size; ++i) {
            batch.emplace_back(std::move(logMessageQueue_.front()));
            logMessageQueue_.pop_front();
        }
        
        lock.unlock(); 
        
        for (auto& [level, message] : batch) {
            if (!message.empty()) { 
                writeToDestination(level, message);
            }
        }
        
        batch.clear();
    }
    
    std::unique_lock<std::mutex> lock(queueMutex_);
    while (!logMessageQueue_.empty()) {
        auto& [level, message] = logMessageQueue_.front();
        if (!message.empty()) {
            writeToDestination(level, message);
        }
        logMessageQueue_.pop_front();
    }
}

void VHLogger::addConsoleSink() {
    
    std::lock_guard<std::mutex> lock(mutex_);
    appendNewSink(VHLogSinkType::ConsoleSink);
}


void VHLogger::addFileSink(const std::string& basePathAndName, std::size_t maxSize) {

    std::lock_guard<std::mutex> lock(mutex_);
    appendNewSink(VHLogSinkType::FileSink);
    
    if (basePathAndName_ == "") {
        basePathAndName_ = basePathAndName;
    }
    
    if (file_ && file_.is_open()) {
        file_.close();
    }
    
    maxSize_ = maxSize;
    currentSize_ = 0;
    
    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::floor<std::chrono::seconds>(now);
    auto zt = std::chrono::zoned_time(std::chrono::current_zone(), nowSec);
    currentDate_ = std::format("{:%Y-%m-%d}", zt);
    std::string fileName = std::format("{}_{:%Y-%m-%d_%H-%M:%S}.log", basePathAndName_, zt);


    file_.open(fileName, std::ios::app);
    if (!file_) {
        std::println("Failed to open/create log file: {}", fileName);
    }
}

void VHLogger::rotateFileSink() {
   
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (file_ && file_.is_open()) {
        file_.flush();
        file_.close();
    }

    currentSize_ = 0;
    unflushedBytes_ = 0;

    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::floor<std::chrono::seconds>(now);
    auto zt = std::chrono::zoned_time(std::chrono::current_zone(), nowSec);
    currentDate_ = std::format("{:%Y-%m-%d}", zt);
    std::string fileName = std::format("{}_{:%Y-%m-%d_%H-%M:%S}.log", basePathAndName_, zt);

    file_.open(fileName, std::ios::app);
    if (!file_) {
        std::println("Failed to open/create log file: {}", fileName);
    }
}

void VHLogger::addNullSink() {
    
    std::lock_guard<std::mutex> lock(mutex_);
    appendNewSink(VHLogSinkType::NullSink);
}

void VHLogger::addTCPSink(const std::string& hostIpAddress, unsigned int hostPort) {

#ifdef USE_ASIO
    std::lock_guard<std::mutex> lock(mutex_);
    appendNewSink(VHLogSinkType::TCPSink);
    hostIpAddress_ = hostIpAddress;
    hostPort_ = hostPort;
    connectTCPSink();
#else
    writeToDestination(VHLogLevel::WARNINGLV, "You are trying to use TCP sink, but you have compiled without asio.");
#endif
}

void VHLogger::log(VHLogLevel level, const std::string& message) {
    
    if (level != VHLogLevel::DEBUGLV || debugEnvironment_) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::pair<VHLogLevel, std::string> received;
        logMessageQueue_.emplace_back(level, std::move(message));
        condVar_.notify_one();
    }
}

void VHLogger::writeToDestination(VHLogLevel level, const std::string& message) {
   
    bool needsTcp = false;
    auto now = std::chrono::system_clock::now();
    auto nowSec = std::chrono::floor<std::chrono::seconds>(now);
    auto zt = std::chrono::zoned_time(std::chrono::current_zone(), nowSec);

    static constexpr const char* levels[] = {
        "DEBUG", "INFO", "WARNING", "ERROR", "FATAL", "UNKNOWN"
    };

    const char* levelString = levels[std::min(static_cast<int>(level), 5)];
    std::string composedMessage = std::format("[{:%Y-%m-%d_%H-%M:%S}] [{}] {}\n",
                                             zt, 
                                             levelString, 
                                             message);
    
    for (const auto& sinkType : sinkTypes_) {
        switch ((int)sinkType) {
            case (int)VHLogSinkType::FileSink:
                {
                    if (file_ && file_.is_open()) {
                        file_ << composedMessage;
                        currentSize_ += composedMessage.size();
                        unflushedBytes_ += composedMessage.size();
                        bool bShouldFlush = false;
                        if (unflushedBytes_ >= FLUSH_THRESHOLD) {
                            bShouldFlush = true;
                        }
                        else if (level == VHLogLevel::FATALLV || level == VHLogLevel::ERRORLV) {
                            bShouldFlush = true;
                        }
                        else if (shouldRotate(composedMessage.size())) {
                            bShouldFlush = true;
                            rotateFileSink();
                        }
                        if (bShouldFlush) {
                            file_.flush();
                            unflushedBytes_ = 0;
                        }
                    }
                }
                break;
            case (int)VHLogSinkType::ConsoleSink:
                std::print("{}", composedMessage);
                break;
            case (int)VHLogSinkType::NullSink:
                break;
            case (int)VHLogSinkType::TCPSink:
                needsTcp = true;
                break;
        }
    }
#ifdef USE_ASIO
    if (needsTcp) {
        if (!shutdownSocket_) {
            std::string tcpMessage = composedMessage;
            asio::post(ioContext_, [this, msg = std::move(tcpMessage) ]() mutable {
                if (!shutdownSocket_) {
                    tcpMessageQueue_.push_back(std::move(msg));
                    if (!tcpIsSending_) {
                        sendNextTCPMessage();
                    }
                }
            });
        }
    }
#endif
}

bool VHLogger::shouldRotate(std::size_t messageSize) {
    if (currentSize_ + messageSize > maxSize_) {
        return true;
    }
    auto now = std::chrono::system_clock::now();
    std::string currentDate = std::format("{:%Y-%m-%d}", now);
    
    if (currentDate != currentDate_) {
        return true;
    }
    return false;
}

#ifdef USE_ASIO
void VHLogger::connectTCPSink() {
    if (shutdownSocket_) {
        return;
    }

    if (reconnectTimer_) {
        reconnectTimer_->cancel();
    }

    std::error_code ec;
    asio::error_code res;

    if (socket_.is_open()) {
        res = socket_.close(ec);
    }
    
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        socketConnected_ = false;
    }
    
    tcpIsSending_ = false;
    
    asio::ip::tcp::resolver resolver(ioContext_);
    auto endpoints = resolver.resolve(hostIpAddress_, std::to_string(hostPort_), ec);
    
    if (ec) {
        if (!shutdownSocket_) {
            scheduleReconnectTCPSink();
        }
        return;
    }
    
    asio::async_connect(socket_, endpoints, 
        [this](std::error_code ec, asio::ip::tcp::endpoint endpoint) {
            if (shutdownSocket_.load(std::memory_order_acquire)) { 
                return;
            }
            if (!ec) {
                std::lock_guard<std::mutex> lock(socketMutex_);
                socketConnected_ = true;
                
                asio::socket_base::keep_alive option(true);
                socket_.set_option(option);
                
                startReadingForDisconnects();
                
                asio::post(ioContext_, [this]() {
                    if (!tcpMessageQueue_.empty() && !tcpIsSending_) {
                        sendNextTCPMessage();
                    }
                });
            } 
            else {
                if (!shutdownSocket_) {
                    scheduleReconnectTCPSink();
                }
            }
        }
    );
}

void VHLogger::sendNextTCPMessage() {
    if (shutdownSocket_.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> lock(socketMutex_);
        tcpIsSending_ = false;
        return;
    }
    
    std::unique_lock<std::mutex> lock(socketMutex_, std::try_to_lock);
    if (!lock.owns_lock() || tcpIsSending_) {
        return;
    }
    
    if (tcpMessageQueue_.empty() || !socketConnected_) {
        tcpIsSending_ = false;
        
        if (!tcpMessageQueue_.empty() && !socketConnected_ && !shutdownSocket_.load(std::memory_order_acquire)) {
            asio::post(ioContext_, [this]() {
                if (!shutdownSocket_.load(std::memory_order_acquire)) {
                    connectTCPSink();
                }
            });
        }
        return;
    }
    
    tcpIsSending_ = true;
    std::string message = std::move(tcpMessageQueue_.front());
    tcpMessageQueue_.pop_front();
    
    if (shutdownSocket_.load(std::memory_order_acquire)) {
        tcpIsSending_ = false;
        return;
    }
    
    auto message_ptr = std::make_shared<std::string>(std::move(message));
    
    asio::async_write(socket_, asio::buffer(*message_ptr),
        [this, message_ptr](std::error_code ec, size_t bytes_written) {
            if (shutdownSocket_.load(std::memory_order_acquire)) {
                std::lock_guard<std::mutex> lock(socketMutex_);
                tcpIsSending_ = false;
                return;
            }
            
            {
                std::lock_guard<std::mutex> lock(socketMutex_);
                tcpIsSending_ = false;
            }
            
            if (ec) {
                if (!shutdownSocket_.load(std::memory_order_acquire)) {
                    std::lock_guard<std::mutex> lock(socketMutex_);
                    if (!shutdownSocket_.load(std::memory_order_acquire)) {
                        tcpMessageQueue_.push_front(std::move(*message_ptr));
                        
                        std::error_code ignored_ec;
                        socket_.close(ignored_ec);
                        socketConnected_ = false;
                        
                        if (!shutdownSocket_.load(std::memory_order_acquire)) {
                            scheduleReconnectTCPSink();
                        }
                    }
                }
            } 
            else {
                if (!shutdownSocket_.load(std::memory_order_acquire)) {
                    bool connected = false;
                    bool hasMore = false;
                    {
                        std::lock_guard<std::mutex> lock(socketMutex_);
                        connected = socketConnected_;
                        hasMore = !tcpMessageQueue_.empty();
                    }
                    
                    if (hasMore && connected) {
                        asio::post(ioContext_, [this]() {
                            if (!shutdownSocket_.load(std::memory_order_acquire)) {
                                sendNextTCPMessage();
                            }
                        });
                    }
                }
            }
        }
    );
}

void VHLogger::startReadingForDisconnects() {
    if (shutdownSocket_) {
        return;
    }
        
    socket_.async_read_some(
        asio::buffer(readBuffer_), [this](std::error_code ec, std::size_t bytes_read) {
            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    {
                        std::lock_guard<std::mutex> lock(socketMutex_);
                        socketConnected_ = false;
                        tcpIsSending_ = false;
                    }
                        
                    if (!shutdownSocket_) {
                        scheduleReconnectTCPSink();
                    }
                }
            }
            else {
                startReadingForDisconnects();
            }
        }
    );
}

void VHLogger::scheduleReconnectTCPSink() {
    if (!reconnectTimer_ || shutdownSocket_) {
        return;
    }
    
    reconnectTimer_->expires_after(std::chrono::seconds(2));
    reconnectTimer_->async_wait(
        [this](std::error_code ec) { 
            if (ec) {
                return;
            }
            
            if (shutdownSocket_) { 
                return;
            }
            
            connectTCPSink(); 
        }
    );
}

#endif


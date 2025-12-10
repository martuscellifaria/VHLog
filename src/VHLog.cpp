#include "../include/VHLog.h"
#include "asio/error_code.hpp"
#include <chrono>
#include <cstddef>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>


#ifdef USE_ASIO
VHLogger::VHLogger(bool debugEnvironment, std::size_t batchSize) : socket_(ioContext_) {

    workerRunning_ = true;
    batchSize_ = batchSize;
    tcpIsSending_ = false;
    unflushedBytes_ = 0;
    debugEnvironment_ = debugEnvironment;
    basePathAndName_ = "";
    sinkTypes_.clear();
    socketConnected_ = false;
    shutdownSocket_ = false;
    reconnectTimer_ = std::make_unique<asio::steady_timer>(ioContext_);

    workGuard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(ioContext_)
    );
    loggerThread_ = std::thread(&VHLogger::loggerWorker, this);
    ioThread_ = std::thread([this] { 
        ioContext_.run(); 
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

VHLogger::~VHLogger() {
   
    workerRunning_ = false;

    condVar_.notify_all();

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

    if (!tcpMessageQueue_.empty()) {
        
        for (int i = 0; i < 50 && !tcpMessageQueue_.empty(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    shutdownSocket_ = true;
    
    {
        std::lock_guard<std::mutex> lock(socketMutex_);
        tcpIsSending_ = false;
        socketConnected_ = false;
    }
    
    std::error_code ec;
    asio::error_code res = socket_.cancel(ec); 
    
    if (reconnectTimer_) {
        reconnectTimer_->cancel();
    }
    
    if (socket_.is_open()) {
        res = socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        res = socket_.close(ec);
    }
    
    tcpMessageQueue_.clear();
    
    ioContext_.stop();
    
    condVar_.notify_all();
    
    if (loggerThread_.joinable()) {
        loggerThread_.join();
    }

    if (workGuard_) {
        workGuard_.reset();
    }
    
    if (ioThread_.joinable()) { 
        ioThread_.join();
    }
    
    if (file_ && file_.is_open()) {
        file_.flush();
        file_.close();
    }
}
#else
VHLogger::VHLogger(bool debugEnvironment, std::size_t batchSize) {
    workerRunning_ = true;
    batchSize_ = batchSize;
    unflushedBytes_ = 0;
    debugEnvironment_ = debugEnvironment;
    basePathAndName_ = "";
    sinkTypes_.clear();
    loggerThread_ = std::thread(&VHLogger::loggerWorker, this);
}

VHLogger::~VHLogger() {
    workerRunning_ = false;
    
    condVar_.notify_all();
    
    if (loggerThread_.joinable()) {
        loggerThread_.join();
    }

    if (file_ && file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

#endif

void VHLogger::loggerWorker() {
    
    std::vector<std::pair<VHLogLevel, std::string>> batch;
    
    while (workerRunning_) {
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

    std::string currentDateTime = getCurrentDateTime();
    currentDate_ = currentDateTime.substr(0, currentDateTime.find('_'));
    std::string timeForFileName = currentDateTime.substr(0, currentDateTime.find(':'));
    
    std::string fileName = VHGlobalFormat(basePathAndName_, "_", timeForFileName, ".log");
    file_.open(fileName, std::ios::app);
    if (!file_) {
        std::cerr << "Failed to open/create log file: " << fileName << '\n';
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

    std::string currentDateTime = getCurrentDateTime();
    currentDate_ = currentDateTime.substr(0, currentDateTime.find('_'));
    std::string timeForFileName = currentDateTime.substr(0, currentDateTime.find(':'));
    
    std::string fileName = VHGlobalFormat(basePathAndName_, "_", timeForFileName, ".log");
    file_.open(fileName, std::ios::app);
    if (!file_) {
        std::cerr << "Failed to open/create log file: " << fileName << '\n';
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

void VHLogger::log(VHLogLevel level, std::string message) {
    
    if (level != VHLogLevel::DEBUGLV || debugEnvironment_) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        std::pair<VHLogLevel, std::string> received;
        received.first = level;
        received.second = message;
        logMessageQueue_.emplace_back(received);
        condVar_.notify_one();
    }
}

void VHLogger::writeToDestination(VHLogLevel level, const std::string& message) {
    
    std::string timestamp = getCurrentDateTime();
    std::string levelString = levelToString(level);
    std::string composedMessage = VHGlobalFormat("[", timestamp, "] [", levelString, "] ", message, "\n");
    
    for (const auto& sinkType : sinkTypes_) {
        switch ((int)sinkType) {
            case (int)VHLogSinkType::FileSink:
                {
                    std::lock_guard<std::mutex> fileLock(fileMutex_);
                    if (file_ && file_.is_open()) {
                        file_ << composedMessage;
                        currentSize_ += (composedMessage.size() + 1);
                        unflushedBytes_ += (composedMessage.size() + 1);
                        bool bShouldFlush = false;
                        if (unflushedBytes_ >= FLUSH_THRESHOLD) {
                            bShouldFlush = true;
                        }
                        else if (level == VHLogLevel::FATALLV || level == VHLogLevel::ERRORLV) {
                            bShouldFlush = true;
                        }
                        else if (shouldRotate(composedMessage.size() + 1)) {
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
                std::cout << composedMessage << '\n';
                break;
            case (int)VHLogSinkType::NullSink:
                break;
            case (int)VHLogSinkType::TCPSink:
#ifdef USE_ASIO
                std::string msgCopy = composedMessage;
                asio::post(ioContext_, [this, msg = std::move(msgCopy) ]() {
                    tcpMessageQueue_.push_back(std::move(msg));
                    if (!tcpIsSending_) {
                        sendNextTCPMessage();
                    }
                });
#endif
                break;
        }
    }
}

std::string VHLogger::getCurrentDateTime() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&now_c, &tm):
#else
    localtime_r(&now_c, &tm);
#endif
    char buffer[20];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H-%M:%S", &tm);
    return buffer;
}

std::string VHLogger::levelToString(VHLogLevel level) {
    switch (level) {
        case VHLogLevel::DEBUGLV:
            return "DEBUG";
        case VHLogLevel::INFOLV:
            return "INFO";
        case VHLogLevel::ERRORLV:
            return "ERROR";
        case VHLogLevel::WARNINGLV:
            return "WARNING";
        case VHLogLevel::FATALLV:
            return "FATAL";
        default:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

bool VHLogger::shouldRotate(std::size_t messageSize) {
    if (currentSize_ + messageSize > maxSize_) {
        return true;
    }
    std::string currentDateTime = getCurrentDateTime();
    std::string currentDate = currentDateTime.substr(0, currentDateTime.find('_'));
    
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
    
    if (shutdownSocket_) {
        {
            std::lock_guard<std::mutex> lock(socketMutex_);
            tcpIsSending_ = false;
        }
        return;
    }
    
    std::unique_lock<std::mutex> lock(socketMutex_, std::try_to_lock);
    if (!lock.owns_lock() || tcpIsSending_) {
        return;
    }

    if (tcpMessageQueue_.empty() || !socketConnected_) {
        tcpIsSending_ = false;
        
        if (!tcpMessageQueue_.empty() && !socketConnected_ && !shutdownSocket_) {
            asio::post(ioContext_, [this]() {
                connectTCPSink();
            });
        }
        return;
    }

    tcpIsSending_ = true;
    std::string message = tcpMessageQueue_.front();
    
    std::string message_copy = message;
    tcpMessageQueue_.pop_front();

    asio::async_write(socket_, asio::buffer(message),
        [this, message_copy](std::error_code ec, size_t bytes_written) {
            {
                std::lock_guard<std::mutex> lock(socketMutex_);
                tcpIsSending_ = false;
            }
            
            if (ec) {
                if (!shutdownSocket_) {
                    tcpMessageQueue_.push_front(message_copy);
                }
                
                std::error_code ignored_ec;
                asio::error_code res;
                res = socket_.close(ignored_ec);
                
                {
                    std::lock_guard<std::mutex> lock(socketMutex_);
                    socketConnected_ = false;
                }
                
                if (!shutdownSocket_) {
                    scheduleReconnectTCPSink();
                }
            } 
            else {
                if (!tcpMessageQueue_.empty() && !shutdownSocket_) {
                    bool connected = false;
                    {
                        std::lock_guard<std::mutex> lock(socketMutex_);
                        connected = socketConnected_;
                    }
                    
                    if (connected) {
                        asio::post(ioContext_, [this]() {
                            sendNextTCPMessage();
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


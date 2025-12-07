#include "../include/VHLog.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>


#ifdef USE_ASIO
VHLogger::VHLogger(bool bDebugEnvironment, std::size_t iBatchSize) : m_asSocket(m_ioContext) {

    m_bWorkerRunning = true;
    m_iBatchSize = iBatchSize;
    m_iUnflushedBytes = 0;
    m_bDebugEnvironment = bDebugEnvironment;
    m_sBasePathAndName = "";
    m_sSinkTypes.clear();
    m_bSocketConnected = false;
    m_bShutdownSocket = false;
    m_atReconnectTimer = std::make_unique<asio::steady_timer>(m_ioContext);

    m_workGuard = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(m_ioContext)
    );
    m_tLoggerThread = std::thread(&VHLogger::loggerWorker, this);
    m_tioThread = std::thread([this] { 
        m_ioContext.run(); 
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

VHLogger::~VHLogger() {
    m_bWorkerRunning = false;
    m_bShutdownSocket = true;
    
    if (m_atReconnectTimer) {
        m_atReconnectTimer->cancel();
    }
    
    m_ioContext.stop();
    m_cCondVar.notify_all();
    
    if (m_tLoggerThread.joinable()) {
        m_tLoggerThread.join();
    }

    if (m_workGuard) {
        m_workGuard.reset(); 
    }
    
    if (m_tioThread.joinable()) { 
        m_tioThread.join();
    }
    
    if (m_fFile && m_fFile.is_open()) {
        m_fFile.flush();
        m_fFile.close();
    }
}
#else
VHLogger::VHLogger(bool bDebugEnvironment, std::size_t iBatchSize) {
    m_bWorkerRunning = true;
    m_iBatchSize = iBatchSize;
    m_iUnflushedBytes = 0;
    m_bDebugEnvironment = bDebugEnvironment;
    m_sBasePathAndName = "";
    m_sSinkTypes.clear();
    m_tLoggerThread = std::thread(&VHLogger::loggerWorker, this);
}

VHLogger::~VHLogger() {
    m_bWorkerRunning = false;
    
    m_cCondVar.notify_all();
    
    if (m_tLoggerThread.joinable()) {
        m_tLoggerThread.join();
    }

    if (m_fFile && m_fFile.is_open()) {
        m_fFile.flush();
        m_fFile.close();
    }
}

#endif

void VHLogger::loggerWorker() {
    
    std::vector<std::pair<VHLogLevel, std::string>> batch;
    
    while (m_bWorkerRunning) {
        std::unique_lock<std::mutex> lock(m_mQueueMutex);
        
        m_cCondVar.wait(lock, [this]() {
            return !m_dLogMessageQueue.empty() || !m_bWorkerRunning;
        });
        
        if (!m_bWorkerRunning && m_dLogMessageQueue.empty()) {
            break;
        }
        
        const size_t available = m_dLogMessageQueue.size();
        const size_t batch_size = std::min(available, static_cast<size_t>(m_iBatchSize));
        
        batch.reserve(batch_size);
        
        for (size_t i = 0; i < batch_size; ++i) {
            batch.emplace_back(std::move(m_dLogMessageQueue.front()));
            m_dLogMessageQueue.pop_front();
        }
        
        lock.unlock(); 
        
        for (auto& [level, message] : batch) {
            if (!message.empty()) { 
                writeToDestination(level, message);
            }
        }
        
        batch.clear();
    }
    
    std::unique_lock<std::mutex> lock(m_mQueueMutex);
    while (!m_dLogMessageQueue.empty()) {
        auto& [level, message] = m_dLogMessageQueue.front();
        if (!message.empty()) {
            writeToDestination(level, message);
        }
        m_dLogMessageQueue.pop_front();
    }
}

void VHLogger::addConsoleSink() {
    
    std::lock_guard<std::mutex> lock(m_mMutex);
    appendNewSink(VHLogSinkType::ConsoleSink);
}


void VHLogger::addFileSink(const std::string& sBasePathAndName, std::size_t iMaxSize) {

    std::lock_guard<std::mutex> lock(m_mMutex);
    appendNewSink(VHLogSinkType::FileSink);
    
    if (m_sBasePathAndName == "") {
        m_sBasePathAndName = sBasePathAndName;
    }
    
    if (m_fFile && m_fFile.is_open()) {
        m_fFile.close();
    }
    
    m_iMaxSize = iMaxSize;
    m_iCurrentSize = 0;

    std::string currentDateTime = getCurrentDateTime();
    m_sCurrentDate = currentDateTime.substr(0, currentDateTime.find('_'));
    std::string timeForFileName = currentDateTime.substr(0, currentDateTime.find(':'));
    
    std::string fileName = VHGlobalFormat(m_sBasePathAndName, "_", timeForFileName, ".log");
    m_fFile.open(fileName, std::ios::app);
    if (!m_fFile) {
        std::cerr << "Failed to open/create log file: " << fileName << '\n';
    }
}

void VHLogger::rotateFileSink() {
    
    std::lock_guard<std::mutex> lock(m_mFileMutex);
    
    if (m_fFile && m_fFile.is_open()) {
        m_fFile.flush();
        m_fFile.close();
    }

    m_iCurrentSize = 0;
    m_iUnflushedBytes = 0;

    std::string currentDateTime = getCurrentDateTime();
    m_sCurrentDate = currentDateTime.substr(0, currentDateTime.find('_'));
    std::string timeForFileName = currentDateTime.substr(0, currentDateTime.find(':'));
    
    std::string fileName = VHGlobalFormat(m_sBasePathAndName, "_", timeForFileName, ".log");
    m_fFile.open(fileName, std::ios::app);
    if (!m_fFile) {
        std::cerr << "Failed to open/create log file: " << fileName << '\n';
    }
}

void VHLogger::addNullSink() {
    
    std::lock_guard<std::mutex> lock(m_mMutex);
    appendNewSink(VHLogSinkType::NullSink);
}

void VHLogger::addTCPSink(const std::string& sHostIPAddress, unsigned int iHostPort) {

#ifdef USE_ASIO
    std::lock_guard<std::mutex> lock(m_mMutex);
    appendNewSink(VHLogSinkType::TCPSink);
    m_sHostIPAdress = sHostIPAddress;
    m_iHostPort = iHostPort;
    connectTCPSink();
#else
    writeToDestination(VHLogLevel::WARNINGLV, "You are trying to use TCP sink, but you have compiled without asio.");
#endif
}

void VHLogger::log(VHLogLevel level, std::string sMessage) {
    
    if (level != VHLogLevel::DEBUGLV || m_bDebugEnvironment) {
        std::lock_guard<std::mutex> lock(m_mQueueMutex);
        std::pair<VHLogLevel, std::string> received;
        received.first = level;
        received.second = sMessage;
        m_dLogMessageQueue.emplace_back(received);
        m_cCondVar.notify_one();
    }
}

void VHLogger::writeToDestination(VHLogLevel level, const std::string& sMessage) {
    
    std::string timestamp = getCurrentDateTime();
    std::string levelString = levelToString(level);
    std::string composedMessage = VHGlobalFormat("[", timestamp, "] [", levelString, "] ", sMessage, "\n");
    
    for (const auto& sinkType : m_sSinkTypes) {
        switch ((int)sinkType) {
            case (int)VHLogSinkType::FileSink:
                {
                    std::lock_guard<std::mutex> fileLock(m_mFileMutex);
                    if (m_fFile && m_fFile.is_open()) {
                        m_fFile << composedMessage;
                        m_iCurrentSize += (composedMessage.size() + 1);
                        m_iUnflushedBytes += (composedMessage.size() + 1);
                        bool bShouldFlush = false;
                        if (m_iUnflushedBytes >= FLUSH_THRESHOLD) {
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
                            m_fFile.flush();
                            m_iUnflushedBytes = 0;
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
                bool connected = false;
                {
                    std::lock_guard<std::mutex> lock(m_socketMutex);
                    connected = m_bSocketConnected;
                }
                if (!connected) {
                    break;
                }
                asio::post(m_ioContext, [this, msg = composedMessage ]() {
                    asio::async_write(m_asSocket, asio::buffer(msg),
                        [this](std::error_code ec, size_t) {
                            if (ec) {
                                scheduleReconnectTCPSink();
                            }
                        }
                    );
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

bool VHLogger::shouldRotate(std::size_t iMessageSize) {
    if (m_iCurrentSize + iMessageSize > m_iMaxSize) {
        return true;
    }
    std::string currentDateTime = getCurrentDateTime();
    std::string currentDate = currentDateTime.substr(0, currentDateTime.find('_'));
    
    if (currentDate != m_sCurrentDate) {
        return true;
    }
    return false;
}

#ifdef USE_ASIO
void VHLogger::connectTCPSink() {
    if (m_bShutdownSocket) {
        return;
    }

    asio::post(m_ioContext, [this]() {
        if (m_bShutdownSocket) {
            return;
        } 
        if (m_asSocket.is_open()) {
            std::error_code ec;
            m_asSocket.close(ec);
        }
        
        {
            std::lock_guard<std::mutex> lock(m_socketMutex);
            m_bSocketConnected = false;
        }
        
        try {
            asio::ip::tcp::resolver resolver(m_ioContext);
            auto endpoints = resolver.resolve(m_sHostIPAdress, std::to_string(m_iHostPort));
            
            asio::async_connect(m_asSocket, endpoints, 
                [this](std::error_code ec, asio::ip::tcp::endpoint endpoint) {
                    if (!ec) {
                        std::lock_guard<std::mutex> lock(m_socketMutex);
                        m_bSocketConnected = true;
                    } 
                    else {
                        if (!m_bShutdownSocket) {
                            scheduleReconnectTCPSink();
                        }
                    }
                }
            );
        } catch (const std::exception& e) {
            std::cerr << "[EXCEPTION] " << e.what() << '\n';
        }
    });
}

void VHLogger::scheduleReconnectTCPSink() {
    if (!m_atReconnectTimer || m_bShutdownSocket) {
        return;
    }
    
    m_atReconnectTimer->expires_after(std::chrono::seconds(2));
    m_atReconnectTimer->async_wait(
        [this](std::error_code ec) { 
            if (ec || m_bShutdownSocket) { 
                return;
            }
            connectTCPSink(); 
        }
    );
}
#endif 

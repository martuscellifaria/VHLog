#include "../include/VHLog.h"
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <string>

VHLogger::VHLogger() {
    m_bWorkerRunning = true;
    m_sBasePathAndName = "";
    m_tLoggerThread = std::thread(&VHLogger::loggerWorker, this);
}

VHLogger::~VHLogger() {
    m_bWorkerRunning = false;
    m_cCondVar.notify_all();

    if (m_tLoggerThread.joinable()) {
        m_tLoggerThread.join();
    }

    if (m_fFile && m_fFile.is_open()) {
        m_fFile.close();
    }
}

void VHLogger::loggerWorker() {

    while (m_bWorkerRunning) {
        std::unique_lock<std::mutex> lock(m_mQueueMutex);
        m_cCondVar.wait(lock, [this]() {
            return !m_dLogMessageQueue.empty() || m_bWorkerRunning;
        });

        while (!m_dLogMessageQueue.empty()) {
            VHLogLevel level = std::move(m_dLogMessageQueue.front().first);
            std::string message = std::move(m_dLogMessageQueue.front().second);
            m_dLogMessageQueue.pop_front();
            lock.unlock();
            writeToDestination(level, message);
            lock.lock();
        }
    }

    while (!m_dLogMessageQueue.empty()) {
        writeToDestination(m_dLogMessageQueue.front().first, m_dLogMessageQueue.front().second);
        m_dLogMessageQueue.pop_front();
    }
}

void VHLogger::addLogSink(VHLogSinkType sinkType, const std::string& sBasePathAndName, std::size_t iMaxSize) {
    std::lock_guard<std::mutex> lock(m_mMutex);
    appendNewSink(sinkType);
    if (sinkType == VHLogSinkType::ConsoleSink || sinkType == VHLogSinkType::NullSink) {
        return;
    }
    else if (sinkType == VHLogSinkType::FileSink) {
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
}

void VHLogger::log(VHLogLevel level, std::string sMessage) {
    std::lock_guard<std::mutex> lock(m_mQueueMutex);
    std::pair<VHLogLevel, std::string> received;
    received.first = level;
    received.second = sMessage;
    m_dLogMessageQueue.emplace_back(received);
    m_cCondVar.notify_one();
}

void VHLogger::writeToDestination(VHLogLevel level, const std::string& sMessage) {
    std::string timestamp = getCurrentDateTime();
    std::string levelString = levelToString(level);
    std::string composedMessage = VHGlobalFormat("[", timestamp, "] [", levelString, "] ", sMessage, "\n");
    
    for (const auto& sinkType : m_sSinkTypes) {
        switch ((int)sinkType) {
            case (int)VHLogSinkType::FileSink:
                if (m_fFile) {
                    m_fFile << composedMessage;
                    m_iCurrentSize += (composedMessage.size() + 1);
                    m_fFile.flush();
                }
                if (sinkType == VHLogSinkType::FileSink) {
                    if (!m_fFile.is_open()) {
                        std::cerr << "Logfile is not open. Won't write." << '\n';
                        break;
                    }
                    else if (shouldRotate(sMessage.size())) {
                        addLogSink(sinkType, m_sBasePathAndName, m_iMaxSize);
                    }
                }
                break;
            case (int)VHLogSinkType::ConsoleSink:
                std::cout << composedMessage << '\n';
                break;
            case (int)VHLogSinkType::NullSink:
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

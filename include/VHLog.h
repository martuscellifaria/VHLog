#pragma once
#include <cstddef>
#include <fstream>
#include <string>
#include <mutex>
#include <ctime>
#include <memory>
#include <deque>
#include <thread>
#include <condition_variable>
#include <utility>
#include <type_traits>

template<typename... Args>
std::string VHGlobalFormat(Args&&... args) {
    std::string result;
    result.reserve(64*sizeof...(args));
    auto append = [&result](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;

        if constexpr (std::is_arithmetic_v<T>) {
            result += std::to_string(arg);
        }
        else if constexpr (std::is_same_v<T, std::string>) {
            result += arg;
        }
        else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char[]>) {
            result += arg;
        }
        else {
            result += std::to_string(arg);
        }
    };

    (append(std::forward<Args>(args)), ...);

    return result;
}

enum class VHLogLevel {
    DEBUGLV,
    INFOLV,
    WARNINGLV,
    ERRORLV
};

enum class VHLogSinkType {
    ConsoleSink,
    FileSink
};

class VHLogger {
public:
    VHLogger();
    virtual ~VHLogger();

    static std::shared_ptr<VHLogger> instance() {
        static auto nfLogger = std::shared_ptr<VHLogger>(new VHLogger);
        return nfLogger;
    }

    void setLogOptions(VHLogSinkType sinkType = VHLogSinkType::ConsoleSink,
            const std::string& sBasePathAndName = "",
            std::size_t iMaxSize = 1024 * 1024);

    void log(VHLogLevel level, std::string sMessage);

private:
    void writeToDestination(VHLogLevel level, const std::string& sMessage);
    std::string getCurrentTime();
    std::string getCurrentDate();
    std::string levelToString(VHLogLevel level);
    bool shouldRotate(std::size_t iMessageSize);

    std::mutex m_mMutex;
    std::ofstream m_fFile;
    std::thread m_tLoggerThread;
    void loggerWorker();
    bool m_bWorkerRunning;

    std::deque<std::pair<VHLogLevel, std::string>> m_dLogMessageQueue;
    std::mutex m_mQueueMutex;
    std::condition_variable m_cCondVar;
    std::string m_sBasePathAndName;
    std::size_t m_iMaxSize;
    std::size_t m_iCurrentSize;
    std::string m_sCurrentDate;
    VHLogSinkType m_sinkType;

};


















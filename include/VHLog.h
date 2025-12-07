#pragma once
#include <cstddef>
#include <fstream>
#include <string>
#include <mutex>
#include <ctime>
#include <memory>
#include <deque>
#include <thread>
#include <set>
#include <condition_variable>
#include <utility>
#include <type_traits>
#include "asio.hpp"
#include "asio/steady_timer.hpp"

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
    ERRORLV,
    FATALLV
};

class VHLogger {
public:
    VHLogger(bool bDebugEnvironment = true);
    virtual ~VHLogger();

    static std::shared_ptr<VHLogger> instance() {
        static auto nfLogger = std::shared_ptr<VHLogger>(new VHLogger);
        return nfLogger;
    }

private:
    enum class VHLogSinkType {
        ConsoleSink,
        FileSink,
        NullSink,
        TCPSink
    };

public:
    void addConsoleSink();
    void addFileSink(const std::string& sBasePathAndName = "", std::size_t iMaxSize = 1024*1024);
    void addNullSink();
    void addTCPSink(const std::string& sHostIPAddress, unsigned int iHostPort);

    void log(VHLogLevel level, std::string sMessage);

private:
    void writeToDestination(VHLogLevel level, const std::string& sMessage);
    void appendNewSink(VHLogSinkType newSink) { m_sSinkTypes.insert(newSink); }
    
    std::string getCurrentDateTime();
    std::string levelToString(VHLogLevel level);
    bool shouldRotate(std::size_t iMessageSize);

    std::mutex m_mMutex;
    std::ofstream m_fFile;
    std::thread m_tLoggerThread;
    void loggerWorker();
    bool m_bWorkerRunning;

    std::deque<std::pair<VHLogLevel, std::string>> m_dLogMessageQueue;
    bool m_bDebugEnvironment;
    std::mutex m_mQueueMutex;
    std::condition_variable m_cCondVar;
    std::string m_sBasePathAndName;
    std::size_t m_iMaxSize;
    std::size_t m_iCurrentSize;
    std::string m_sCurrentDate;
    std::set<VHLogSinkType> m_sSinkTypes;

    // TCPSink with asio
    asio::io_context m_ioContext;
    std::unique_ptr<asio::steady_timer> m_atReconnectTimer;
    std::string m_sHostIPAdress;
    unsigned int m_iHostPort;
    void connectTCPSink();
    void scheduleReconnectTCPSink();
    std::atomic<bool> m_bSocketConnected;
    bool m_bShutdownSocket;
    asio::ip::tcp::socket m_asSocket;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> m_workGuard;
    std::thread m_tioThread;
    mutable std::mutex m_socketMutex;
};

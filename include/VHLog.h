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

#ifdef USE_ASIO
#include "asio.hpp"
#endif

enum class VHLogLevel {
    DEBUGLV,
    INFOLV,
    WARNINGLV,
    ERRORLV,
    FATALLV
};

class VHLogger {
public:
    explicit VHLogger(bool debugEnvironment = true, std::size_t batchSize = 1);
    void shutdown();
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
    void addFileSink(const std::string& basePathAndName = "", std::size_t maxSize = 1024*1024);
    void addNullSink();
    void addTCPSink(const std::string& hostIpAddress, unsigned int hostPort);

    void log(VHLogLevel level, const std::string& message);

private:
    void writeToDestination(VHLogLevel level, const std::string& message);
    void appendNewSink(VHLogSinkType newSink) { sinkTypes_.insert(newSink); }
    bool shouldRotate(std::size_t messageSize);
    void rotateFileSink();

    std::mutex mutex_;
    std::mutex fileMutex_;
    std::ofstream file_;
    std::size_t unflushedBytes_;
    std::thread loggerThread_;
    void loggerWorker();
    bool workerRunning_;
    std::size_t batchSize_;

    std::deque<std::pair<VHLogLevel, std::string>> logMessageQueue_;
    bool debugEnvironment_;
    std::mutex queueMutex_;
    std::condition_variable condVar_;
    std::string basePathAndName_;
    std::size_t maxSize_;
    std::size_t currentSize_;
    std::string currentDate_;
    std::set<VHLogSinkType> sinkTypes_;
    static constexpr std::size_t FLUSH_THRESHOLD = 4096;
    bool vhlogShutdown_;
#ifdef USE_ASIO 
    // TCPSink with asio
    asio::io_context ioContext_;
    std::unique_ptr<asio::steady_timer> reconnectTimer_;
    std::string hostIpAddress_;
    unsigned int hostPort_;
    std::string readBuffer_;
    void connectTCPSink();
    void startReadingForDisconnects();
    void scheduleReconnectTCPSink();
    std::atomic<bool> socketConnected_;
    std::atomic<bool> shutdownSocket_{false};
    asio::ip::tcp::socket socket_;
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> workGuard_;
    std::thread ioThread_;
    mutable std::mutex socketMutex_;
    std::deque<std::string> tcpMessageQueue_;
    bool tcpIsSending_;
    void sendNextTCPMessage();
#endif
};


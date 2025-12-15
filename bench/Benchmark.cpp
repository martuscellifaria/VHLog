#include "VHLog.h"
#include <atomic>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>

struct BenchmarkResult {
    std::string test_name;
    size_t threads;
    int messages;
    double elapsed_seconds;
    size_t messages_per_second;
    std::string sink_config;
};

std::vector<BenchmarkResult> g_results;
std::ofstream g_results_file;

void bench(int howmany, VHLogger& logger, const std::string& test_name, size_t threads, const std::string& sink_config);
void bench_mt(int howmany, VHLogger& logger, size_t thread_count, const std::string& test_name, const std::string& sink_config);

static const size_t file_size = 30 * 1024 * 1024;
static const int max_threads = 1000;

void init_results_file() {
    g_results_file.open("vhlog_benchmark_results.txt", std::ios::out | std::ios::app);
    if (!g_results_file) {
        std::cerr << "Failed to open results file!\n";
        return;
    }
    
    // Write header with timestamp
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    g_results_file << "==============================================================\n";
    g_results_file << "VHLog Benchmark Results - " << std::ctime(&t);
    g_results_file << "==============================================================\n\n";
}

void save_result(const BenchmarkResult& result) {
    g_results.push_back(result);
    
    if (g_results_file) {
        g_results_file << "Test: " << result.test_name << "\n";
        g_results_file << "Sinks: " << result.sink_config << "\n";
        g_results_file << "Threads: " << result.threads << "\n";
        g_results_file << "Messages: " << result.messages << "\n";
        g_results_file << "Time: " << std::fixed << std::setprecision(3) << result.elapsed_seconds << " sec\n";
        
        std::string rate_str = std::to_string(result.messages_per_second);
        for (int i = rate_str.length() - 3; i > 0; i -= 3) {
            rate_str.insert(i, ",");
        }
        
        g_results_file << "Rate: " << rate_str << " msg/sec\n";
        g_results_file << "----------------------------------------\n";
        g_results_file.flush();
    }
}

void print_summary() {
    if (!g_results_file) return;
    
    g_results_file << "\n\n==============================================================\n";
    g_results_file << "SUMMARY (Sorted by Performance)\n";
    g_results_file << "==============================================================\n";
    
    // Sort by performance (highest first)
    std::sort(g_results.begin(), g_results.end(), 
              [](const BenchmarkResult& a, const BenchmarkResult& b) {
                  return a.messages_per_second > b.messages_per_second;
              });
    
    for (const auto& result : g_results) {
        std::string rate_str = std::to_string(result.messages_per_second);
        for (int i = rate_str.length() - 3; i > 0; i -= 3) {
            rate_str.insert(i, ",");
        }
        
        g_results_file << std::setw(40) << std::left << result.test_name 
                      << std::setw(20) << result.sink_config
                      << std::setw(8) << result.threads
                      << std::setw(12) << rate_str << " msg/sec\n";
    }
    
    g_results_file << "\n";
    g_results_file.close();
}

void print_header(const std::string& title, size_t threads, int iters) {
    std::cout << "\n**************************************************************\n";
    std::cout << title << "\n";
    if (threads > 1) {
        std::cout << "Threads: " << threads << ", Messages: " << iters << "\n";
    } else {
        std::cout << "Messages: " << iters << "\n";
    }
    std::cout << "**************************************************************\n";
    
    if (g_results_file) {
        g_results_file << "\n" << title << "\n";
        if (threads > 1) {
            g_results_file << "Threads: " << threads << ", Messages: " << iters << "\n";
        } else {
            g_results_file << "Messages: " << iters << "\n";
        }
    }
}

void bench_threaded_logging(size_t threads, int iters) {
    print_header("VHLog Multi-threaded Benchmarks", threads, iters);

    {
        VHLogger basic_mt(false, 100);
        basic_mt.addFileSink("logs/basic_mt.log", file_size);
        std::cout << "\n[Basic File Sink]\n";
        bench_mt(iters, basic_mt, threads, "Basic File Sink", "File only");
    }

    {
        VHLogger rotating_mt(false, 100);
        rotating_mt.addFileSink("logs/rotating_mt", file_size);
        std::cout << "\n[Date-based Rotating File Sink]\n";
        bench_mt(iters, rotating_mt, threads, "Date-based Rotating", "File (date rotation)");
    }

    {
        VHLogger console_mt(false, 100);
        console_mt.addConsoleSink();
        std::cout << "\n[Console Sink]\n";
        bench_mt(iters, console_mt, threads, "Console Only", "Console only");
    }

    {
        VHLogger multi_mt(false, 100);
        multi_mt.addFileSink("logs/multi_mt.log", file_size);
        multi_mt.addConsoleSink();
        std::cout << "\n[Multi-sink: File + Console]\n";
        bench_mt(iters, multi_mt, threads, "File+Console", "File + Console");
    }

    {
        VHLogger null_mt(false, 100);
        null_mt.addNullSink();
        std::cout << "\n[Null Sink (baseline)]\n";
        bench_mt(iters, null_mt, threads, "Null Sink", "Null (no output)");
    }

    {
        VHLogger tcp_mt(false, 100);
        #ifdef USE_ASIO
        tcp_mt.addTCPSink("127.0.0.1", 9000);
        std::cout << "\n[TCP Sink]\n";
        bench_mt(iters, tcp_mt, threads, "TCP Sink", "TCP (127.0.0.1:9000)");
        #else
        std::cout << "\n[TCP Sink - ASIO not compiled]\n";
        #endif
    }

    {
        VHLogger all_mt(false, 100);
        all_mt.addFileSink("logs/all_mt.log", file_size);
        all_mt.addConsoleSink();
        #ifdef USE_ASIO
        all_mt.addTCPSink("127.0.0.1", 9000);
        std::cout << "\n[All Sinks Combined]\n";
        bench_mt(iters, all_mt, threads, "All Sinks", "File + Console + TCP");
        #else
        std::cout << "\n[File+Console Sinks]\n";
        bench_mt(iters, all_mt, threads, "File+Console", "File + Console");
        #endif
    }
}

void bench_single_threaded(int iters) {
    print_header("VHLog Single-threaded Benchmarks", 1, iters);

    {
        VHLogger basic_st(false, 100);
        basic_st.addFileSink("logs/basic_st.log", file_size);
        std::cout << "\n[Basic File Sink]\n";
        bench(iters, basic_st, "Basic File (ST)", 1, "File only");
    }

    {
        VHLogger console_st(false, 100);
        console_st.addConsoleSink();
        std::cout << "\n[Console Sink]\n";
        bench(iters, console_st, "Console Only (ST)", 1, "Console only");
    }

    {
        VHLogger multi_st(false, 100);
        multi_st.addFileSink("logs/multi_st.log", file_size);
        multi_st.addConsoleSink();
        std::cout << "\n[Multi-sink: File + Console]\n";
        bench(iters, multi_st, "File+Console (ST)", 1, "File + Console");
    }

    // Null sink
    {
        VHLogger null_st(false, 100);
        null_st.addNullSink();
        std::cout << "\n[Null Sink (baseline)]\n";
        bench(iters, null_st, "Null Sink (ST)", 1, "Null (no output)");
    }
}

int main(int argc, char *argv[]) {
    int iters = 250000;
    size_t threads = 4;
    
    try {
        init_results_file();
        
        if (argc > 1) {
            iters = std::stoi(argv[1]);
        }
        if (argc > 2) {
            threads = std::stoul(argv[2]);
        }

        if (threads > max_threads) {
            throw std::runtime_error("Number of threads exceeds maximum(" + 
                                    std::to_string(max_threads) + ")");
        }

        system("mkdir -p logs");

        std::cout << "==============================================================\n";
        std::cout << "VHLog Performance Benchmarks\n";
        std::cout << "Results will be saved to: vhlog_benchmark_results.txt\n";
        std::cout << "==============================================================\n";

        bench_single_threaded(iters);
        bench_threaded_logging(1, iters);
        bench_threaded_logging(threads, iters);

        print_summary();
        
        std::cout << "\nBenchmark complete! Results saved to vhlog_benchmark_results.txt\n";
        
        std::cout << "\nQuick Summary:\n";
        std::cout << "--------------\n";
        for (const auto& result : g_results) {
            if (result.threads == threads) {
                std::string rate_str = std::to_string(result.messages_per_second);
                for (int i = rate_str.length() - 3; i > 0; i -= 3) {
                    rate_str.insert(i, ",");
                }
                std::cout << std::setw(30) << std::left << result.test_name 
                          << ": " << std::setw(12) << rate_str << " msg/sec\n";
            }
        }

    } catch (std::exception &ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        if (g_results_file) g_results_file.close();
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}

void bench(int howmany, VHLogger& logger, const std::string& test_name, 
           size_t threads, const std::string& sink_config) {
    using namespace std::chrono;
    
    VHLogger result_logger(false, 10);
    result_logger.addFileSink("vhlog_benchmark_results.txt", 0);
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < howmany; ++i) {
        logger.log(VHLogLevel::INFOLV, "Hello logger: msg number " + std::to_string(i));
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto delta = high_resolution_clock::now() - start;
    auto delta_d = duration_cast<duration<double>>(delta).count();
    auto rate = static_cast<size_t>(howmany / delta_d);
    
    BenchmarkResult result{
        test_name,
        threads,
        howmany,
        delta_d,
        rate,
        sink_config
    };
    save_result(result);
    
    std::string rate_str = std::to_string(rate);
    for (int i = rate_str.length() - 3; i > 0; i -= 3) {
        rate_str.insert(i, ",");
    }
    
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(2) << delta_d 
              << " secs  " << std::setw(12) << rate_str << "/sec\n";
}

void bench_mt(int howmany, VHLogger& logger, size_t thread_count, 
              const std::string& test_name, const std::string& sink_config) {
    using namespace std::chrono;
    
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    
    auto start = high_resolution_clock::now();
    
    for (size_t t = 0; t < thread_count; ++t) {
        threads.emplace_back([&logger, howmany, thread_count, t]() {
            int per_thread = howmany / static_cast<int>(thread_count);
            for (int j = 0; j < per_thread; j++) {
                logger.log(VHLogLevel::INFOLV, "Hello logger: msg number " + std::to_string(j));
            }
        });
    }

    for (auto &t : threads) {
        t.join();
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    auto delta = high_resolution_clock::now() - start;
    auto delta_d = duration_cast<duration<double>>(delta).count();
    auto rate = static_cast<size_t>(howmany / delta_d);
    
    BenchmarkResult result{
        test_name,
        thread_count,
        howmany,
        delta_d,
        rate,
        sink_config
    };
    save_result(result);
    
    std::string rate_str = std::to_string(rate);
    for (int i = rate_str.length() - 3; i > 0; i -= 3) {
        rate_str.insert(i, ",");
    }
    
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(2) << delta_d 
              << " secs  " << std::setw(12) << rate_str << "/sec\n";
}

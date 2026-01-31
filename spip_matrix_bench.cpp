#include "spip_matrix.h"
#include "TelemetryLogger.h"

void run_thread_test(const Config& cfg, int num_threads) {
    int n = (num_threads > 0) ? num_threads : cfg.concurrency;
    std::cout << MAGENTA << "🧪 Benchmarking Concurrency Orchestration (" << n << " threads)..." << RESET << std::endl;
    std::string test_id = "bench_threads_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::unique_ptr<TelemetryLogger> tel = cfg.telemetry ? std::make_unique<TelemetryLogger>(cfg, test_id) : nullptr;
    if (tel) tel->start();
    auto start = std::chrono::steady_clock::now(); std::vector<std::thread> workers; std::atomic<int> run_count{0};
    for (int i = 0; i < n; ++i) workers.emplace_back([&]() { run_count++; volatile double sink = 0; for(long j = 0; j < 10000000; ++j) sink += (double)j * j; (void)sink; run_count--; });
    bool mon = true; int peak = 0; std::thread monitor([&]() { while(mon) { int cur = run_count.load(); if (cur > peak) peak = cur; std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
    for (auto& t : workers) t.join(); mon = false; monitor.join();
    auto end = std::chrono::steady_clock::now(); double wall = std::chrono::duration<double>(end - start).count();
    if (tel) tel->stop();
    std::cout << "\n🏁 Thread Test: Target=" << n << ", Peak=" << peak << ", Time=" << wall << "s" << std::endl;
}

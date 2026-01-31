#include "spip_matrix.h"

int benchmark_concurrency(const Config& cfg) {
    std::cout << MAGENTA << "🔍 Benchmarking network..." << RESET << std::endl;
    std::vector<int> tests = {1, 4, 8, 16, 32}; std::string url = "https://files.pythonhosted.org/packages/ef/b5/b4b38202d659a11ff928174ad4ec0725287f3b89b88f343513a8dd645d94/idna-3.7-py3-none-any.whl";
    fs::path tmp = cfg.spip_root / "bench.whl"; int best_c = 4; double min_t = 1e9;
    for (int c : tests) {
        if (c > (int)std::thread::hardware_concurrency() * 4) break;
        auto s = std::chrono::steady_clock::now(); std::vector<std::thread> workers; 
        for (int i = 0; i < c; ++i) workers.emplace_back([&, i]() { run_shell(std::format("timeout -s 9 4s curl -L -s {} -o {}_{}", url, tmp.string(), i).c_str()); });
        for (auto& t : workers) t.join(); auto e = std::chrono::steady_clock::now(); double d = std::chrono::duration<double>(e - s).count();
        if (d > 0 && d < min_t) { min_t = d; best_c = c; }
        for (int i = 0; i < c; ++i) { std::error_code ec; fs::remove(std::format("{}_{}", tmp.string(), i), ec); }
    }
    return best_c;
}

void parallel_download(const Config& cfg, const std::vector<PackageInfo>& info_list) {
    if (info_list.empty()) return;
    std::queue<PackageInfo> q; for (const auto& info : info_list) if (!fs::exists(cfg.spip_root / (info.name + "-" + info.version + ".whl"))) q.push(info);
    if (q.empty()) return;
    int c = (cfg.concurrency > 0) ? cfg.concurrency : benchmark_concurrency(cfg);
    std::mutex m; std::atomic<int> comp{0}; int tot = q.size();
    auto worker = [&]() {
        while (!g_interrupted) {
            PackageInfo info; { std::lock_guard<std::mutex> l(m); if (q.empty()) return; info = q.front(); q.pop(); }
            fs::path t = cfg.spip_root / (info.name + "-" + info.version + ".whl"); fs::path p = t.string() + ".part." + std::to_string(getpid());
            if (run_shell(std::format("timeout 300 curl -f -L --connect-timeout 10 --max-time 240 -s -# {} -o {}", quote_arg(info.wheel_url), quote_arg(p.string())).c_str()) == 0) fs::rename(p, t);
            else if (fs::exists(p)) fs::remove(p);
            std::cout << "\rProgress: " << ++comp << "/" << tot << std::flush;
        }
    };
    std::vector<std::thread> ts; for (int i = 0; i < c; ++i) ts.emplace_back(worker);
    for (auto& t : ts) t.join();
}

#include "spip_db.h"

void fetch_package_metadata(const Config& cfg, const std::string& pkg) {
    fs::path target = get_db_path(pkg); if (fs::exists(target) && fs::file_size(target) > 0) return;
    static std::mutex m_fetch; std::lock_guard<std::mutex> lock(m_fetch);
    if (fs::exists(target) && fs::file_size(target) > 0) return;
    fs::create_directories(target.parent_path());
    std::string url = std::format("{}/pypi/{}/json", cfg.pypi_mirror, pkg);
    fs::path temp_target = target; temp_target += ".tmp";
    if (run_shell(std::format("curl -s -L \"{}\" -o \"{}\"", url, temp_target.string()).c_str()) == 0 && fs::exists(temp_target)) fs::rename(temp_target, target);
}

void db_worker(std::queue<std::string>& q, std::mutex& m, std::atomic<int>& count, int total, Config cfg) {
    while (true) {
        std::string pkg; { std::lock_guard<std::mutex> lock(m); if (q.empty()) return; pkg = q.front(); q.pop(); }
        fetch_package_metadata(cfg, pkg); int c = ++count;
        if (c % 100 == 0) std::cout << "\rFetched " << c << "/" << total << std::flush;
    }
}


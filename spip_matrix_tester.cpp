#include "spip_matrix_tester.h"
#include "spip_db.h"
#include "spip_env.h"
#include "spip_python.h"

void MatrixTester::run(const std::string& custom_test_script, const std::string& python_version, bool profile, bool no_cleanup, int revision_limit, bool test_all_revisions, bool vary_python, int pkg_revision_limit, const std::string& pinned_pkg_ver) {
    auto versions = select_versions(vary_python, revision_limit, test_all_revisions, pkg_revision_limit, pinned_pkg_ver);
    if (versions.empty()) return;
    std::string test_run_id = pkg + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    std::unique_ptr<TelemetryLogger> tel = cfg.telemetry ? std::make_unique<TelemetryLogger>(cfg, test_run_id) : nullptr;
    if (tel) tel->start();
    std::map<std::string, PackageInfo> all_needed; std::mutex m_needed; std::vector<std::future<void>> futures;
    auto task = [&](const std::string& ver) {
        auto res = vary_python ? (ver.find(':') != std::string::npos ? resolve_only({pkg}, split(ver, ':')[1], split(ver, ':')[0]) : resolve_only({pkg}, "", ver)) : resolve_only({pkg}, ver, python_version);
        std::lock_guard<std::mutex> l(m_needed); for (const auto& [id, info] : res) all_needed[id] = info;
    };
    for (const auto& ver : versions) { if (futures.size() >= (size_t)cfg.concurrency * 2) { for(auto& f : futures) f.wait(); futures.clear(); } futures.push_back(std::async(std::launch::async, task, ver)); }
    for(auto& f : futures) f.wait();
    std::vector<PackageInfo> info_list; for (const auto& [id, info] : all_needed) info_list.push_back(info);
    parallel_download(cfg, info_list);
    // ... (continued)
}

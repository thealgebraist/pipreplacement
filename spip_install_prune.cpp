#include "spip_install.h"
#include "spip_env.h"

void prune_orphans(const Config& cfg) {
    fs::path sp = get_site_packages(cfg); if (sp.empty()) return;
    fs::path mf = cfg.project_env_path / ".spip_manual"; std::set<std::string> mp;
    if (fs::exists(mf)) { std::ifstream ifs(mf); std::string line; while (std::getline(ifs, line)) if (!line.empty()) mp.insert(line); }
    std::cout << MAGENTA << "🧹 Identifying orphaned packages..." << RESET << std::endl;
    std::set<std::string> req; std::vector<std::string> q(mp.begin(), mp.end()); size_t i = 0;
    while(i < q.size()) {
        std::string n = q[i++]; if (req.count(n)) continue; req.insert(n);
        PackageInfo info = get_package_info(n); for (const auto& d : info.dependencies) { std::string ld = d; std::transform(ld.begin(), ld.end(), ld.begin(), ::tolower); q.push_back(ld); }
    }
    std::set<std::string> inst; for (const auto& entry : fs::directory_iterator(sp)) {
        if (entry.is_directory()) { std::string n = entry.path().filename().string(); if (n.ends_with(".dist-info")) { std::string p = n.substr(0, n.find('-')); std::transform(p.begin(), p.end(), p.begin(), ::tolower); inst.insert(p); } }
    }
    std::vector<std::string> prune; for (const auto& pkg : inst) if (!req.count(pkg)) prune.push_back(pkg);
    if (prune.empty()) { std::cout << GREEN << "✨ No orphans found." << RESET << std::endl; return; }
    std::cout << YELLOW << "Found " << prune.size() << " orphans. Pruning..." << RESET << std::endl;
    for (const auto& p : prune) uninstall_package(cfg, p);
    commit_state(cfg, "Pruned orphans"); std::cout << GREEN << "✔️  Orphan pruning complete." << RESET << std::endl;
}

fs::path get_site_packages(const Config& cfg) {
    if (!fs::exists(cfg.project_env_path)) return fs::path();
    try { for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) if (entry.is_directory() && entry.path().filename() == "site-packages") return entry.path(); } catch (...) {}
    return fs::path();
}

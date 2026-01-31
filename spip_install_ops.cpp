#include "spip_install.h"

void uninstall_package(const Config& cfg, const std::string& pkg) {
    fs::path sp = get_site_packages(cfg); if (sp.empty()) return;
    std::string low = pkg; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    std::string norm = low; std::replace(norm.begin(), norm.end(), '-', '_');
    fs::path dist; for (const auto& entry : fs::directory_iterator(sp)) {
        std::string ln = entry.path().filename().string(); std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
        if (ln.find(norm) == 0 && ln.ends_with(".dist-info")) { dist = entry.path(); break; }
    }
    if (dist.empty()) { std::cout << RED << "❌ Could not find installation metadata for " << pkg << RESET << std::endl; return; }
    std::cout << MAGENTA << "🗑 Uninstalling " << BOLD << pkg << RESET << "..." << std::endl;
    fs::path record = dist / "RECORD"; if (fs::exists(record)) {
        std::ifstream ifs(record); std::string line; while (std::getline(ifs, line)) {
            size_t comma = line.find(','); if (comma != std::string::npos) {
                fs::path full = sp / line.substr(0, comma); if (fs::exists(full) && !fs::is_directory(full)) {
                    fs::remove(full); fs::path p = full.parent_path();
                    while (p != sp && fs::exists(p) && fs::is_empty(p)) { fs::remove(p); p = p.parent_path(); }
                }
            }
        }
    }
    fs::remove_all(dist);
}

void record_manual_install(const Config& cfg, const std::string& pkg, bool add) {
    fs::path f = cfg.project_env_path / ".spip_manual"; std::set<std::string> pkgs;
    if (fs::exists(f)) { std::ifstream ifs(f); std::string line; while (std::getline(ifs, line)) if (!line.empty()) pkgs.insert(line); }
    std::string low = pkg; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (add) pkgs.insert(low); else pkgs.erase(low);
    std::ofstream ofs(f); for (const auto& p : pkgs) ofs << p << "\n";
}

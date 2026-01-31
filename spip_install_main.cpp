#include "spip_install.h"

bool install_single_package(const Config& cfg, const PackageInfo& info, const fs::path& site_packages);

bool resolve_and_install(const Config& cfg, const std::vector<std::string>& targets, const std::string& version, const std::string& target_py) {
    std::vector<std::string> queue = targets; std::set<std::string> installed; std::map<std::string, PackageInfo> resolved;
    std::cout << MAGENTA << "🔍 Resolving dependencies..." << RESET << std::endl;
    size_t i = 0; while(i < queue.size()) {
        std::string name = queue[i++]; std::string low = name; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        std::replace(low.begin(), low.end(), '_', '-'); std::replace(low.begin(), low.end(), '.', '-');
        if (installed.count(low)) continue;
        PackageInfo info = (i == 1 && !version.empty()) ? get_package_info(name, version, target_py) : get_package_info(name, "", target_py);
        if (info.wheel_url.empty()) { std::cout << RED << "❌ Could not find wheel URL for " << name << RESET << std::endl; continue; }
        resolved[low] = info; installed.insert(low); for (const auto& d : info.dependencies) queue.push_back(d);
    }
    fs::path sp = get_site_packages(cfg); if (sp.empty()) { std::cerr << RED << "❌ Could not find site-packages directory." << RESET << std::endl; return false; }
    std::cout << GREEN << "🚀 Installing " << resolved.size() << " packages..." << RESET << std::endl;
    bool all_ok = true; int current = 0;
    for (const auto& [id, info] : resolved) {
        std::string norm = info.name; std::transform(norm.begin(), norm.end(), norm.begin(), ::tolower); std::replace(norm.begin(), norm.end(), '-', '_');
        bool skip = false; for (const auto& entry : fs::directory_iterator(sp)) {
            std::string en = entry.path().filename().string(); std::transform(en.begin(), en.end(), en.begin(), ::tolower); std::replace(en.begin(), en.end(), '-', '_');
            if (en == norm || (en.find(norm + "-") == 0 && en.find(".dist-info") != std::string::npos)) { skip = true; break; }
        }
        if (skip) { std::cout << GREEN << "✔ " << info.name << " already installed." << RESET << std::endl; continue; }
        std::cout << BLUE << "[" << ++current << "/" << resolved.size() << "] " << RESET << "📦 " << BOLD << info.name << RESET << " (" << info.version << ")..." << std::endl;
        if (!install_single_package(cfg, info, sp)) all_ok = false;
    }
    return all_ok;
}

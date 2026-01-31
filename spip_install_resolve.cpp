#include "spip_install.h"

bool resolve_and_install(const Config& cfg, const std::vector<std::string>& targets, const std::string& version, const std::string& target_py) {
    std::vector<std::string> queue = targets; std::set<std::string> installed; std::map<std::string, PackageInfo> resolved;
    std::cout << MAGENTA << "🔍 Resolving dependencies..." << RESET << std::endl;
    size_t i = 0; while(i < queue.size()) {
        std::string name = queue[i++]; std::string lower_name = name; std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        std::replace(lower_name.begin(), lower_name.end(), '_', '-'); std::replace(lower_name.begin(), lower_name.end(), '.', '-');
        if (installed.count(lower_name)) continue;
        PackageInfo info = (i == 1 && !version.empty()) ? get_package_info(name, version, target_py) : get_package_info(name, "", target_py);
        if (info.wheel_url.empty()) { std::cout << RED << "❌ Could not find wheel URL for " << name << RESET << std::endl; continue; }
        resolved[lower_name] = info; installed.insert(lower_name); for (const auto& d : info.dependencies) queue.push_back(d);
    }
    fs::path site_packages = get_site_packages(cfg);
    if (site_packages.empty()) { std::cerr << RED << "❌ Could not find site-packages directory." << RESET << std::endl; return false; }
    std::cout << GREEN << "🚀 Installing " << resolved.size() << " packages..." << RESET << std::endl;
    int current = 0; bool all_ok = true;
    for (const auto& [id, info] : resolved) {
        std::string norm_name = info.name; std::transform(norm_name.begin(), norm_name.end(), norm_name.begin(), ::tolower);
        std::replace(norm_name.begin(), norm_name.end(), '-', '_'); bool already_installed = false;
        for (const auto& entry : fs::directory_iterator(site_packages)) {
            std::string entry_name = entry.path().filename().string(); std::transform(entry_name.begin(), entry_name.end(), entry_name.begin(), ::tolower);
            std::string norm_entry = entry_name; std::replace(norm_entry.begin(), norm_entry.end(), '-', '_');
            if (norm_entry == norm_name || (norm_entry.find(norm_name + "-") == 0 && norm_entry.find(".dist-info") != std::string::npos)) { already_installed = true; break; }
        }
        if (already_installed) { std::cout << GREEN << "✔ " << info.name << " " << info.version << " already installed." << RESET << std::endl; continue; }
        // ... (continued in next part)
    }
    return all_ok;
}

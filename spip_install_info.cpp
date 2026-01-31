#include "spip_install.h"
#include "spip_db.h"
#include "spip_env.h"

PackageInfo get_package_info(const std::string& pkg, const std::string& version, const std::string& target_py) {
    fs::path db_file = get_db_path(pkg);
    if (!fs::exists(db_file)) { static std::set<std::string> shown; if (!shown.count(pkg)) { std::cout << YELLOW << "⚠️ Metadata for " << pkg << " not in local DB. Fetching..." << RESET << std::endl; shown.insert(pkg); } Config cfg = init_config(); fetch_package_metadata(cfg, pkg); }
    std::ifstream ifs(db_file); std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    PackageInfo info; if (content.empty()) return info; info.name = pkg;
    if (version.empty()) {
        size_t info_pos = content.find("info");
        if (info_pos != std::string::npos) {
            size_t ver_key_pos = content.find("version", info_pos);
            if (ver_key_pos != std::string::npos) {
                size_t val_start = content.find("\"", content.find(":", ver_key_pos));
                if (val_start != std::string::npos) info.version = content.substr(val_start + 1, content.find("\"", val_start + 1) - val_start - 1);
            }
        }
        if (info.version.empty()) info.version = extract_field(content, "version");
    } else info.version = version;
    info.requires_python = extract_field(content, "requires_python");
    auto raw_deps = extract_array(content, "requires_dist");
    for (const auto& d : raw_deps) {
        std::regex dep_re(R"(^([a-zA-Z0-9_.-]+)([^;]*)(;.*)?)"); std::smatch m;
        if (std::regex_search(d, m, dep_re) && (m[3].str().find("extra ==") == std::string::npos)) info.dependencies.push_back(m[1].str());
    }
    size_t rel_pos = content.find("releases");
    if (rel_pos != std::string::npos) {
        std::string ver_key = "\"" + info.version + "\""; size_t ver_entry = content.find(ver_key, rel_pos);
        if (ver_entry != std::string::npos) {
            size_t cur = content.find("[", ver_entry) + 1; int bal = 1;
            while (cur < content.size() && bal > 0) { if (content[cur] == '[') bal++; else if (content[cur] == ']') bal--; cur++; }
            std::string release_data = content.substr(content.find("[", ver_entry), cur - content.find("[", ver_entry));
            std::regex url_re(R"(\"url\":\s*\"(https\://[^\"]*\.whl)\")"); auto wheels_begin = std::sregex_iterator(release_data.begin(), release_data.end(), url_re);
            int best_score = -1; for (auto it = wheels_begin; it != std::sregex_iterator(); ++it) {
                std::string url = (*it)[1].str(); int s = score_wheel(url, target_py); if (s > best_score) { best_score = s; info.wheel_url = url; }
            }
        }
    }
    return info;
}
#include "spip_db.h"
#include "spip_env.h"

std::vector<std::string> get_all_versions(const std::string& pkg) {
    fs::path db_file = get_db_path(pkg);
    if (!fs::exists(db_file)) {
        Config cfg = init_config(); fetch_package_metadata(cfg, pkg);
    }
    std::vector<std::string> versions;
    if (fs::exists(db_file)) {
        std::ifstream ifs(db_file);
        std::string json((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()); 
        size_t rel_pos = json.find("releases");
        if (rel_pos != std::string::npos) {
            // Match keys that map to arrays [ ... ] which is the structure of releases
            std::regex ver_re(R"(\"([^\"]+)\"\s*:\s*\[)");
            auto begin = std::sregex_iterator(json.begin() + rel_pos, json.end(), ver_re);
            auto end = std::sregex_iterator();
            for (std::sregex_iterator i = begin; i != end; ++i) {
                // heuristic: stop if we hit something that looks like the start of another top-level key (unlikely in standardized JSON but good safety)
                if ((*i).position() > 20 && json.substr(rel_pos + (*i).position() - 2, 2) == "},") break; 
                versions.push_back((*i)[1].str());
            }
        }
    }
    std::stable_sort(versions.begin(), versions.end(), [](const std::string& a, const std::string& b) {
        auto parse_ver = [](std::string_view s) {
            std::vector<int> parts; std::string part;
            for (char c : s) if (isdigit(c)) part += c; else { if(!part.empty()) parts.push_back(std::stoi(part)); part=""; }
            if (!part.empty()) parts.push_back(std::stoi(part)); return parts;
        };
        return parse_ver(a) < parse_ver(b);
    });
    return versions;
}

#include "spip_matrix.h"
#include "spip_install.h"

std::map<std::string, PackageInfo> resolve_only(const std::vector<std::string>& targets, const std::string& version, const std::string& target_py) {
    std::vector<std::string> q = targets; std::set<std::string> v; std::map<std::string, PackageInfo> res; size_t i = 0;
    while(i < q.size()) {
        std::string n = q[i++]; std::string ln = n; std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
        std::replace(ln.begin(), ln.end(), '_', '-'); std::replace(ln.begin(), ln.end(), '.', '-');
        if (v.count(ln)) continue;
        PackageInfo info = (i == 1 && !version.empty()) ? get_package_info(n, version, target_py) : get_package_info(n, "", target_py);
        if (info.wheel_url.empty()) continue;
        res[ln + "-" + info.version] = info; v.insert(ln);
        for (const auto& d : info.dependencies) q.push_back(d);
    }
    return res;
}

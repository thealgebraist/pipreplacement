#include "spip_install.h"

int score_wheel(const std::string& url, const std::string& target_py) {
    int score = 0; std::string lower = url; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("macosx") != std::string::npos) {
        if (lower.find("arm64") != std::string::npos) score += 1000;
        else if (lower.find("universal2") != std::string::npos) score += 500;
        else if (lower.find("x86_64") != std::string::npos) score += 100;
    } else if (lower.find("none-any.whl") != std::string::npos) score += 50;
    else return -1;
    std::string py_tag = "cp" + target_py; py_tag.erase(std::remove(py_tag.begin(), py_tag.end(), '.'), py_tag.end());
    if (lower.find(py_tag) != std::string::npos) score += 200;
    else if (lower.find("py3-none-any") != std::string::npos || lower.find("py2.py3-none-any") != std::string::npos) score += 100;
    return score;
}

fs::path get_cached_wheel_path(const Config& cfg, const PackageInfo& info) {
    return cfg.spip_root / (info.name + "-" + info.version + ".whl");
}

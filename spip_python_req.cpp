#include "spip_python.h"

std::string parse_python_requirement(const std::string& req) {
    if (req.empty()) return "3";
    std::regex re(R"((\d+\.\d+))"); std::smatch m; std::vector<std::string> mentioned;
    auto search = req; while (std::regex_search(search, m, re)) { mentioned.push_back(m[1].str()); search = m.suffix().str(); }
    if (mentioned.empty()) return "3.12";
    std::string best = "3.12";
    for (const auto& v : mentioned) if (v.starts_with("3.") && v <= "3.13" && v > best) best = v;
    return best;
}

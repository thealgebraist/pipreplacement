#include "spip_db.h"
#include "spip_utils.h"

void benchmark_mirrors(Config& cfg) {
    std::cout << MAGENTA << "🏎  Benchmarking mirrors..." << RESET << std::endl;
    std::vector<std::pair<std::string, std::string>> m = { {"PyPI Official", "https://pypi.org"}, {"Tsinghua", "https://pypi.tuna.tsinghua.edu.cn"}, {"USTC", "https://pypi.mirrors.ustc.edu.cn"}, {"Baidu", "https://mirror.baidu.com/pypi"}, {"Aliyun", "https://mirrors.aliyun.com/pypi"} };
    std::string best = "https://pypi.org"; double min_t = 9999.0;
    for (const auto& [name, url] : m) {
        std::string cmd = std::format("timeout -s 9 4s curl -o /dev/null -s -w \"{{time_total}}\" -m 3 \"{}/pypi/pip/json\"", url);
        try {
            double t = std::stod(get_exec_output(cmd));
            if (t > 0 && t < min_t) { min_t = t; best = url; }
            std::cout << "  - [" << name << "] " << url << ": " << GREEN << t << "s" << RESET << std::endl;
        } catch (...) { std::cout << "  - [" << name << "] " << url << ": " << RED << "Timeout/Error" << RESET << std::endl; }
    }
    cfg.pypi_mirror = best; std::cout << GREEN << "✨ Selected " << best << RESET << std::endl;
}

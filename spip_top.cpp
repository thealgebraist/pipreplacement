#include "spip_top.h"
#include "spip_utils.h"
#include "spip_db.h"
#include "spip_install.h"

void show_top_downloads();
void show_top_references();
void show_top_dependencies();

void show_top_packages(bool refs, bool deps) {
    if (refs) show_top_references();
    else if (deps) show_top_dependencies();
    else show_top_downloads();
}

void show_top_downloads() {
    std::cout << MAGENTA << "🏆 Top 10 PyPI Packages (Downloads)..." << RESET << std::endl;
    std::string j = get_exec_output("curl -s https://hugovk.github.io/top-pypi-packages/top-pypi-packages-30-days.json");
    size_t pos = 0; for (int r = 1; r <= 10; ++r) {
        size_t pk = j.find("\"project\":", pos); if (pk == std::string::npos) break;
        size_t s = j.find("\"", pk + 10); size_t e = j.find("\"", s + 1);
        std::string n = j.substr(s + 1, e - s - 1);
        size_t dk = j.find("\"download_count\":", e); size_t ev = j.find_first_of(",}", dk);
        std::string d = j.substr(dk + 17, ev - (dk + 17));
        std::cout << std::format("{:<5} {:<30} {:<15}", r, n, d) << std::endl; pos = ev;
    }
}


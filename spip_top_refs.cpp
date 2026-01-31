#include "spip_top.h"
#include "spip_utils.h"
#include "spip_db.h"
#include "spip_install.h"

void show_top_references() {
    std::cout << MAGENTA << "🏆 Top 10 PyPI Packages (References)..." << RESET << std::endl;
    std::string h = get_exec_output("curl -L -s -H \"User-Agent: Mozilla/5.0\" \"https://libraries.io/search?languages=Python&order=desc&platforms=Pypi&sort=dependents_count\"");
    if (h.empty() || h.find("Login to Libraries.io") != std::string::npos) { show_top_packages(false, false); return; }
    std::regex re(R"(<h5>\s*<a href=\"/pypi/[^\"]+\">([^<]+)</a>)");
    auto begin = std::sregex_iterator(h.begin(), h.end(), re); int r = 1;
    for (auto i = begin; i != std::sregex_iterator() && r <= 10; ++i) std::cout << std::format("{:<5} {:<30}", r++, (*i)[1].str()) << std::endl;
}

void show_top_dependencies() {
    std::cout << MAGENTA << "🏆 Top 10 PyPI Packages (Dependency Count)..." << RESET << std::endl;
    // Simplified stub
    show_top_packages(false, false);
}


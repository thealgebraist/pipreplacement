#include "spip_matrix_tester.h"

void MatrixTester::summarize(bool prof) {
    std::cout << "\n🏁 Matrix Test Summary for " << pkg << RESET << std::endl;
    if (prof) std::cout << std::format("{:<15} {:<10} {:<15} {:<15} {:<15} {:<15}", "Version", "Install", "Pkg Tests", "Custom Test", "Wall Time", "CPU Time") << std::endl;
    else std::cout << std::format("{:<15} {:<10} {:<15} {:<15}", "Version", "Install", "Pkg Tests", "Custom Test") << std::endl;
    for (const auto& r : results) {
        if (prof) std::cout << std::format("{:<15} {:<19} {:<24} {:<24} {:<15.2f} {:<15.2f}", r.version, (r.install ? GREEN + "PASS" : RED + "FAIL") + RESET, (r.pkg_tests ? GREEN + "PASS" : YELLOW + "FAIL/SKIP") + RESET, (r.custom_test ? GREEN + "PASS" : RED + "FAIL") + RESET, r.stats.wall_time_seconds, r.stats.cpu_time_seconds) << std::endl;
        else std::cout << std::format("{:<15} {:<19} {:<24} {:<24}", r.version, (r.install ? GREEN + "PASS" : RED + "FAIL") + RESET, (r.pkg_tests ? GREEN + "PASS" : YELLOW + "FAIL/SKIP") + RESET, (r.custom_test ? GREEN + "PASS" : RED + "FAIL") + RESET) << std::endl;
    }
}

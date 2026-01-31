#include "spip_test.h"
#include "spip_install.h"

void print_tree(const std::string& pkg, int depth, std::set<std::string>& visited) {
    std::string indent = ""; for (int i = 0; i < depth; ++i) indent += "  ";
    std::string low = pkg; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    if (visited.count(low)) { std::cout << indent << "└── " << YELLOW << pkg << " (circular)" << RESET << std::endl; return; }
    visited.insert(low); PackageInfo info = get_package_info(pkg);
    std::cout << indent << (depth == 0 ? "" : "└── ") << GREEN << pkg << RESET << " (" << info.version << ")" << std::endl;
    for (const auto& dep : info.dependencies) print_tree(dep, depth + 1, visited);
}

#include "spip_test.h"
#include "spip_install.h"

void run_package_tests(const Config& cfg, const std::string& pkg) {
    const fs::path sp = get_site_packages(cfg);
    if (sp.empty()) {
        std::cerr << RED << "❌ site-packages not found." << RESET << std::endl;
        return;
    }
    std::string low = pkg;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    fs::path path;
    for (const auto& entry : fs::directory_iterator(sp)) {
        std::string n = entry.path().filename().string();
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n == low || (n.find(low) == 0 && !n.ends_with(".dist-info"))) {
            path = entry.path();
            break;
        }
    }
    if (path.empty()) {
        std::cerr << RED << "❌ Could not locate package " << pkg << RESET << std::endl;
        return;
    }
    std::cout << MAGENTA << "🧪 Testing " << pkg << "..." << RESET << std::endl;
    const fs::path bin = cfg.project_env_path / "bin" / "python";
    const std::string py_check = std::format("{} -c \"import importlib.util; exit(0 if importlib.util.find_spec('pytest') else 1)\"", quote_arg(bin.string()));
    if (run_shell(py_check.c_str()) != 0) {
        const std::string inst_pytest = std::format("{} -m pip install pytest", quote_arg(bin.string()));
        run_shell(inst_pytest.c_str());
    }
    const std::string test_cmd = std::format("{} -m pytest {}", quote_arg(bin.string()), quote_arg(path.string()));
    run_shell(test_cmd.c_str());
}

void run_all_package_tests(const Config& cfg) {
    const fs::path sp = get_site_packages(cfg);
    if (sp.empty()) return;
    std::set<std::string> pkgs;
    for (const auto& entry : fs::directory_iterator(sp)) {
        if (entry.is_directory()) {
            const std::string n = entry.path().filename().string();
            if (n != "__pycache__" && !n.ends_with(".dist-info") && !n.ends_with(".egg-info") && n != "bin") {
                pkgs.insert(n);
            }
        }
    }
    for (const auto& pkg : pkgs) run_package_tests(cfg, pkg);
}

#include "spip_bundle.h"
#include "spip_env.h"

void generate_setup_py(const fs::path& target_dir, const std::string& pkg_name);

void bundle_package(const Config& cfg, const std::string& path) {
    fs::path target_dir = fs::absolute(path);
    if (!fs::exists(target_dir) || !fs::is_directory(target_dir)) {
        std::cerr << RED << "❌ Target directory not found: " << path << RESET << std::endl;
        return;
    }
    std::string pkg_name = target_dir.filename().string();
    std::cout << MAGENTA << "📦 Bundling C++23 package '" << pkg_name << "'..." << RESET << std::endl;
    if (!fs::exists(target_dir / "setup.py")) generate_setup_py(target_dir, pkg_name);
    setup_project_env(const_cast<Config&>(cfg));
    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    if (run_shell(std::format("{} -m pip --version >/dev/null 2>&1", quote_arg(python_bin.string())).c_str()) != 0) {
        run_shell(std::format("{} -m ensurepip --upgrade", quote_arg(python_bin.string())).c_str());
    }
    std::cout << BLUE << "🚀 Installing package..." << RESET << std::endl;
    if (run_shell(std::format("cd {} && {} -m pip install .", quote_arg(target_dir.string()), quote_arg(python_bin.string())).c_str()) == 0) {
        std::cout << GREEN << "✔️  Package installed successfully." << RESET << std::endl;
    }
}

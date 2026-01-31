#include "spip_test.h"
#include "spip_install.h"

void freeze_environment(const Config& cfg, const std::string& output_file) {
    const fs::path sp = get_site_packages(cfg);
    if (sp.empty()) {
        std::cerr << RED << "❌ site-packages not found." << RESET << std::endl;
        return;
    }
    std::cout << MAGENTA << "🧊 Freezing environment to " << output_file << "..." << RESET << std::endl;
    const std::string cmd = std::format("tar -czf \"{}\" -C \"{}\" . -C \"{}\" pyvenv.cfg", output_file, sp.string(), cfg.project_env_path.string());
    if (run_shell(cmd.c_str()) == 0) {
        std::cout << GREEN << "✨ Environment frozen successfully!" << RESET << std::endl;
    }
}

void audit_environment(const Config& cfg) {
    const fs::path sp = get_site_packages(cfg);
    if (sp.empty()) return;
    std::cout << MAGENTA << "🛡 Performing security audit (OSV API)..." << RESET << std::endl;
    const fs::path h = cfg.spip_root / "scripts" / "audit_helper.py";
    const fs::path py = cfg.project_env_path / "bin" / "python";
    const std::string cmd = std::format("{} {} {}", quote_arg(py.string()), quote_arg(h.string()), quote_arg(sp.string()));
    run_shell(cmd.c_str());
}
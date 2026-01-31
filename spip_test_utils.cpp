#include "spip_test.h"
#include "spip_install.h"

void freeze_environment(const Config& cfg, const std::string& output_file) {
    fs::path sp = get_site_packages(cfg); if (sp.empty()) { std::cerr << RED << "❌ site-packages not found." << RESET << std::endl; return; }
    std::cout << MAGENTA << "🧊 Freezing environment to " << output_file << "..." << RESET << std::endl;
    if (run_shell(std::format("tar -czf \"{output_file}\" -C \"{sp.string()}\" . -C \"{cfg.project_env_path.string()}\" pyvenv.cfg", output_file, sp.string(), cfg.project_env_path.string()).c_str()) == 0) std::cout << GREEN << "✨ Environment frozen successfully!" << RESET << std::endl;
}

void audit_environment(const Config& cfg) {
    fs::path sp = get_site_packages(cfg); if (sp.empty()) return;
    std::cout << MAGENTA << "🛡 Performing security audit (OSV API)..." << RESET << std::endl;
    fs::path h = cfg.spip_root / "scripts" / "audit_helper.py"; fs::path py = cfg.project_env_path / "bin" / "python";
    run_shell(std::format("\"{py.string()}\" \"{h.string()}\" \"{sp.string()}\", py.string(), h.string(), sp.string()).c_str());
}


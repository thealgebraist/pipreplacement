#include "spip_test.h"
#include "spip_install.h"

void review_code(const Config& cfg) {
    const char* k = std::getenv("GEMINI_API_KEY");
    if (!k) { std::cout << YELLOW << "⚠️ GEMINI_API_KEY missing." << RESET << std::endl; return; }
    std::cout << MAGENTA << "🤖 Preparing AI Code Review (Gemini Pro)..." << RESET << std::endl;
    fs::path h = cfg.spip_root / "scripts" / "review_helper.py";
    fs::path py = cfg.project_env_path / "bin" / "python";
    std::string cmd = std::format("\"{}\" \"{}\" \"{}\" \"{}\"", py.string(), h.string(), k, cfg.current_project.string());
    run_shell(cmd.c_str());
}

void verify_environment(const Config& cfg) {
    fs::path sp = get_site_packages(cfg); if (sp.empty()) return;
    std::cout << MAGENTA << "🔍 Verifying environment integrity..." << RESET << std::endl;
    fs::path h = cfg.spip_root / "scripts" / "verify_helper.py";
    fs::path py = cfg.project_env_path / "bin" / "python";
    std::string cmd = std::format("{} {} {} {}", quote_arg(py.string()), quote_arg(h.string()), quote_arg(sp.string()), quote_arg((cfg.project_env_path / "bin").string()));
    if (run_shell(cmd.c_str()) != 0) {
        std::cout << RED << "❌ VERIFICATION FAILED!" << RESET << std::endl;
        std::string reset_cmd = std::format("cd \"{}\" && git reset --hard HEAD^", cfg.project_env_path.string());
        run_shell(reset_cmd.c_str()); std::exit(1);
    } else std::cout << GREEN << "✨ Verification complete." << RESET << std::endl;
}
#include "spip_env.h"

Config init_config() {
    const char* home = std::getenv("HOME");
    if (!home) { std::cerr << RED << "Error: HOME environment variable not set." << RESET << std::endl; std::exit(1); }
    Config cfg;
    cfg.home_dir = fs::path(home);
    cfg.spip_root = cfg.home_dir / ".spip";
    cfg.repo_path = cfg.spip_root / "repo";
    cfg.envs_root = cfg.spip_root / "envs";
    cfg.current_project = fs::current_path();
    cfg.project_hash = compute_hash(cfg.current_project.string());
    cfg.project_env_path = cfg.envs_root / cfg.project_hash;
    cfg.db_file = cfg.spip_root / "knowledge_base.db";
    return cfg;
}

void ensure_scripts(const Config& cfg) {
    fs::path scripts_dir = cfg.spip_root / "scripts";
    if (!fs::exists(scripts_dir)) fs::create_directories(scripts_dir);
    std::vector<std::string> names = { "safe_extract.py", "audit_helper.py", "review_helper.py", "verify_helper.py", "trim_helper.py", "agent_helper.py", "pyc_profiler.py", "profile_ai_review.py" };
    fs::path project_scripts = fs::current_path() / "scripts";
    if (fs::exists(project_scripts)) {
        for (const auto& name : names) {
            fs::path src = project_scripts / name; fs::path dst = scripts_dir / name;
            if (fs::exists(src)) fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
        }
    }
}

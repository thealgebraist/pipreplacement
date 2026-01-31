#include "spip_env.h"
#include "spip_python.h"

void setup_project_env(Config& cfg, const std::string& version) {
    ensure_dirs(cfg); std::string branch = "project/" + cfg.project_hash;
    if (!branch_exists(cfg, branch)) {
        std::string base_branch = "base/" + version;
        if (!branch_exists(cfg, base_branch)) create_base_version(cfg, version);
        std::cout << MAGENTA << "🔨 Bootstrapping base Python " << version << "..." << RESET << std::endl;
        create_base_version(cfg, version);
        std::cout << GREEN << "🌟 Creating new environment branch: " << branch << RESET << std::endl;
        std::string cmd = std::format("cd {} && git branch {} {}", quote_arg(cfg.repo_path.string()), quote_arg(branch), quote_arg(base_branch));
        run_shell(cmd.c_str());
    }
    if (!fs::exists(cfg.project_env_path)) {
        std::cout << CYAN << "📂 Linking worktree for project..." << RESET << std::endl;
        run_shell(std::format("cd {} && git checkout main 2>/dev/null", quote_arg(cfg.repo_path.string())).c_str());
        std::string cmd = std::format("cd {} && git worktree add {} {}", quote_arg(cfg.repo_path.string()), quote_arg(cfg.project_env_path.string()), quote_arg(branch));
        if (run_shell(cmd.c_str()) != 0) {
            run_shell(std::format("cd {} && git worktree prune", quote_arg(cfg.repo_path.string())).c_str());
            run_shell(cmd.c_str());
        }
        std::ofstream os(cfg.project_env_path / ".project_origin"); os << cfg.current_project.string();
    }
}

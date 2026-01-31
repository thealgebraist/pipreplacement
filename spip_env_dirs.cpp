#include "spip_env.h"

void ensure_envs_tmpfs(const Config& cfg) {
    (void)cfg;
#ifdef __linux__
    if (std::getenv("SPIP_NO_TMPFS")) return;
    std::string mount_check = std::format("mount | grep {}", quote_arg(cfg.envs_root.string()));
    if (get_exec_output(mount_check).empty()) {
        std::cout << MAGENTA << "🚀 Mounting " << cfg.envs_root << " as tmpfs for ultra-speed..." << RESET << std::endl;
        std::string mount_cmd = std::format("sudo mount -t tmpfs -o size=2G tmpfs {}", quote_arg(cfg.envs_root.string()));
        run_shell(mount_cmd.c_str());
    }
#endif
}

void ensure_dirs(const Config& cfg) {
    if (!fs::exists(cfg.spip_root)) fs::create_directories(cfg.spip_root);
    if (!fs::exists(cfg.envs_root)) fs::create_directories(cfg.envs_root);
    ensure_envs_tmpfs(cfg); ensure_scripts(cfg);
    if (!fs::exists(cfg.repo_path)) {
        std::cout << "Creating repo at: " << cfg.repo_path << std::endl;
        fs::create_directories(cfg.repo_path);
        std::string cmd = std::format("cd {} && git init && git commit --allow-empty -m \"Initial commit\"", quote_arg(cfg.repo_path.string()));
        run_shell(cmd.c_str());
        std::ofstream gitignore(cfg.repo_path / ".gitignore");
        gitignore << "# Full environment tracking\n"; gitignore.close();
    }
}

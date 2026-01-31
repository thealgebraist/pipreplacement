#include "spip_env_cleanup.h"
#include "spip_utils.h"
#include "ResourceProfiler.h"

void show_usage_stats(const Config& cfg) {
    uintmax_t r = get_dir_size(cfg.repo_path); uintmax_t e = get_dir_size(cfg.envs_root);
    uintmax_t d = get_dir_size(cfg.spip_root / "db"); uintmax_t t = get_dir_size(cfg.spip_root);
    std::cout << BOLD << "📊 Disk Usage:" << RESET << "\n  - Repo: " << (r / 1048576) << " MB\n  - Envs: " << (e / 1048576) << " MB\n  - DB: " << (d / 1048576) << " MB\n  - Total: " << (t / 1048576) << " MB" << std::endl;
}

void cleanup_envs(Config& cfg, bool all);

void cleanup_spip(Config& cfg, bool remove_all) {
    std::cout << MAGENTA << "🧹 Starting cleanup..." << RESET << std::endl;
    show_usage_stats(cfg); cleanup_envs(cfg, remove_all);
    std::cout << MAGENTA << "🗑 Removing temp files..." << RESET << std::endl;
    for (const auto& entry : fs::directory_iterator(cfg.spip_root)) {
        std::string n = entry.path().filename().string();
        if (n.starts_with("temp_venv_")) fs::remove_all(entry.path());
        else if (entry.is_regular_file() && (n.ends_with(".whl") || n.ends_with(".tmp") || n.ends_with(".py"))) fs::remove(entry.path());
    }
    std::cout << GREEN << "✨ Cleanup complete." << RESET << std::endl; show_usage_stats(cfg);
}

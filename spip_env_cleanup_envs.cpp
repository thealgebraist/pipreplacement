#include "spip_env_cleanup.h"
#include "spip_utils.h"

void cleanup_envs(Config& cfg, bool all) {
    if (!fs::exists(cfg.envs_root)) return;
    for (const auto& entry : fs::directory_iterator(cfg.envs_root)) {
        if (!entry.is_directory()) continue;
        bool rem = all; std::string p;
        if (!all) {
            fs::path of = entry.path() / ".project_origin";
            if (fs::exists(of)) { std::ifstream ifs(of); std::getline(ifs, p); if (!p.empty() && !fs::exists(p)) rem = true; }
            else rem = true;
        }
        if (rem) {
            std::string h = entry.path().filename().string();
            run_shell(std::format("cd {} && git worktree remove --force {} 2>/dev/null", quote_arg(cfg.repo_path.string()), quote_arg(entry.path().string())).c_str());
            run_shell(std::format("cd {} && git branch -D project/{} 2>/dev/null", quote_arg(cfg.repo_path.string()), quote_arg(h)).c_str());
            if (fs::exists(entry.path())) fs::remove_all(entry.path());
        }
    }
}

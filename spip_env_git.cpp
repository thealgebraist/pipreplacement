#include "spip_env.h"

bool branch_exists(const Config& cfg, const std::string& branch) {
    std::string cmd = std::format("cd {} && git rev-parse --verify {}", quote_arg(cfg.repo_path.string()), quote_arg(branch));
    std::string out = get_exec_output(cmd);
    if (out.empty() || out.find("fatal") != std::string::npos || out.find("error") != std::string::npos) return false;
    return true;
}

void commit_state(const Config& cfg, const std::string& msg) {
    std::string cmd = std::format("cd {} && git add -A && git commit -m {} --allow-empty", quote_arg(cfg.project_env_path.string()), quote_arg(msg));
    run_shell(cmd.c_str());
}

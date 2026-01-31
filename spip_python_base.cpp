#include "spip_python.h"
#include "spip_env.h"

void create_base_version(const Config& cfg, const std::string& version) {
    std::string branch = "base/" + version; if (branch_exists(cfg, branch)) return;
    std::cout << MAGENTA << "🔨 Bootstrapping base Python " << version << "..." << RESET << std::endl;
    std::string safe_v = ""; for(char c : version) if(std::isalnum(c) || c == '.') safe_v += c;
    fs::path temp_venv = cfg.spip_root / ("temp_venv_" + safe_v);
    std::string python_bin = ensure_python_bin(cfg, safe_v);
    if (run_shell(std::format("{} -m venv {}", quote_arg(python_bin), quote_arg(temp_venv.string()))).c_str()) {
        std::cerr << RED << "❌ Failed to create venv with " << python_bin << RESET << std::endl; std::exit(1);
    }
    std::string curr_br_cmd = "git symbolic-ref --short HEAD 2>/dev/null || echo HEAD";
    std::string curr_br = get_exec_output(std::format("cd {} && {}", quote_arg(cfg.repo_path.string()), curr_br_cmd));
    if (curr_br.empty()) curr_br = "main";
    else if (curr_br.find('\n') != std::string::npos) curr_br = curr_br.substr(0, curr_br.find('\n'));
    std::string git_cmd = std::format(
        "cd \"{}\" && git checkout -b \"{}\" && "
        "find . -mindepth 1 -maxdepth 1 -not -name \".git\" -exec rm -f {{}} \\; 2>/dev/null || true && "
        "cp -r \"{}/\"* . && git add -A && git commit -m \"Base Python {} \" && "
        "git checkout {}", cfg.repo_path.string(), branch, temp_venv.string(), version, curr_br);
    if (run_shell(git_cmd.c_str()) != 0) {
        std::cerr << RED << "❌ Failed to commit base version " << version << RESET << std::endl;
        fs::remove_all(temp_venv); std::exit(1);
    }
    fs::remove_all(temp_venv);
}

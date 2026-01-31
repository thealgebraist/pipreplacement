#include "spip_cmd.h"
#include "spip_env.h"
#include "spip_install.h"
#include "spip_test.h"

void run_command_install(Config& cfg, const std::vector<std::string>& args) {
    setup_project_env(cfg);
    std::vector<std::string> targets; std::string pkg_str = "";
    for (size_t i = 1; i < args.size(); ++i) { 
        targets.push_back(args[i]); pkg_str += args[i] + " "; 
        record_manual_install(cfg, args[i], true);
    }
    if (targets.empty()) { std::cout << "Usage: spip install <packages>" << std::endl; return; }
    if (resolve_and_install(cfg, targets)) {
        commit_state(cfg, "Manually installed " + pkg_str);
        std::cout << GREEN << "✔ Environment updated." << RESET << std::endl;
        verify_environment(cfg);
    } else std::cerr << RED << "❌ Installation failed." << RESET << std::endl;
}

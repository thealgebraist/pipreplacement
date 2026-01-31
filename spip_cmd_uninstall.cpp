#include "spip_cmd.h"
#include "spip_env.h"
#include "spip_install.h"

void run_command_uninstall(Config& cfg, const std::vector<std::string>& args) {
    setup_project_env(cfg); if (args.size() < 2) { std::cout << "Usage: spip uninstall <pkgs>\n"; return; }
    std::string ps = ""; for (size_t i = 1; i < args.size(); ++i) { 
        uninstall_package(cfg, args[i]); ps += args[i] + " "; 
        record_manual_install(cfg, args[i], false);
    }
    commit_state(cfg, "Uninstalled " + ps);
}


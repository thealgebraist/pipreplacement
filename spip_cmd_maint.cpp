#include "spip_cmd.h"
#include "spip_env.h"
#include "spip_install.h"
#include "spip_test.h"
#include "spip_env_cleanup.h"

void run_command_maintenance(Config& cfg, const std::vector<std::string>& args) {
    std::string cmd = args[0];
    if (cmd == "prune") exec_with_setup(cfg, prune_orphans);
    else if (cmd == "audit") exec_with_setup(cfg, audit_environment);
    else if (cmd == "review") exec_with_setup(cfg, review_code);
    else if (cmd == "verify") { setup_project_env(cfg); verify_environment(cfg); }
    else if (cmd == "cleanup" || cmd == "gc") { bool remove_all = (args.size() > 1 && args[1] == "--all"); cleanup_spip(cfg, remove_all); }
    else run_command_matrix(cfg, args);
}
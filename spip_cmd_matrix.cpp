#include "spip_cmd.h"
#include "spip_env.h"
#include "spip_matrix.h"
#include "spip_distributed.h"

void run_command_matrix(Config& cfg, const std::vector<std::string>& args) {
    std::string cmd = args[0];
    if (cmd == "matrix") {
        ensure_dirs(cfg); if (args.size() < 2) return;
        std::string pkg = ""; std::string test_script = ""; std::string python_ver = "auto";
        bool profile = false; bool telemetry = false; bool no_cleanup = false;
        int revision_limit = -1; bool test_all_revisions = false; bool smoke_test = false;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "--python" && i + 1 < args.size()) python_ver = args[++i];
            else if (arg == "--profile") profile = true;
            else if (arg == "--telemetry") telemetry = true;
            else if (arg == "--smoke") smoke_test = true;
            else if (arg == "--no-cleanup") no_cleanup = true;
            else if (arg == "--limit" && i + 1 < args.size()) revision_limit = std::stoi(args[++i]);
            else if (arg == "--all") test_all_revisions = true;
            else if (arg.starts_with("--")) continue;
            else { if (pkg.empty()) pkg = arg; else if (test_script.empty()) test_script = arg; }
        }
        if (pkg.empty()) return;
        Config m_cfg = cfg; m_cfg.telemetry = telemetry;
        if (smoke_test) run_thread_test(m_cfg);
        matrix_test(m_cfg, pkg, test_script, python_ver, profile, no_cleanup, revision_limit, test_all_revisions, false);
    }
}

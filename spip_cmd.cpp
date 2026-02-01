#include "spip_cmd.h"
#include "spip_env.h"
#include "spip_install.h"
#include "spip_test.h"
#include "spip_matrix.h"
#include "spip_distributed.h"
#include "spip_db.h"
#include "spip_bundle.h"

void run_command(Config& cfg, const std::vector<std::string>& args) {
    if (args.empty()) { std::cout << "Usage: spip <cmd> [args...]\n" << std::endl; return; }
    std::string cmd = args[0];
    if (cmd == "diff") {
        std::vector<char*> argv_vec;
        for (size_t i = 1; i < args.size(); i++) {
            argv_vec.push_back(const_cast<char*>(args[i].c_str()));
        }
        cmd_diff(argv_vec.size(), argv_vec.data());
    } else if (cmd == "bundle") { if (require_args(args, 2, "Usage: spip bundle <folder>")) bundle_package(cfg, args[1]); }
    else if (cmd == "boot") { if (require_args(args, 2, "Usage: spip boot <script.py>")) { setup_project_env(cfg); boot_environment(cfg, args[1]); } }
    else if (cmd == "fetch-db") {
        init_db(); std::ifstream f("all_packages.txt"); if (!f.is_open()) return;
        std::queue<std::string> q; std::string l; while (std::getline(f, l)) if (!l.empty()) q.push(l);
        std::mutex m; std::atomic<int> c{0}; std::vector<std::thread> ts;
        for (int i = 0; i < 16; ++i) ts.emplace_back(db_worker, std::ref(q), std::ref(m), std::ref(c), q.size(), cfg);
        for (auto& t : ts) t.join();
        run_shell(std::format("cd {} && git add packages && git commit -m \"Update DB\"", quote_arg(cfg.repo_path.parent_path().string() + "/db")).c_str());
    } else if (cmd == "top") run_command_top(cfg, args);
    else if (cmd == "install" || cmd == "i") run_command_install(cfg, args);
    else if (cmd == "uninstall" || cmd == "remove") run_command_uninstall(cfg, args);
    else run_command_maintenance(cfg, args);
}
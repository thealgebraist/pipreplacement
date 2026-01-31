#include "spip_matrix_tester.h"
#include "spip_env.h"

void MatrixTester::run_execution_phase(const std::string& custom_test_script, const std::string& python_version, bool profile, bool no_cleanup, int revision_limit, bool test_all_revisions, bool vary_python, int pkg_revision_limit, const std::string& pinned_pkg_ver) {
    // ... (logic from spip_matrix_tester.cpp continued)
    // Storage for Wheels
    std::string wb = "wheels"; if (branch_exists(cfg, wb) || run_shell(std::format("cd {} && git branch {}", quote_arg(cfg.repo_path.string()), wb).c_str()) == 0) {
        fs::path wwt = cfg.spip_root / "wheels_wt"; if (!fs::exists(wwt)) run_shell(std::format("cd {} && git worktree add --detach {} {}", quote_arg(cfg.repo_path.string()), quote_arg(wwt.string()), wb).c_str());
        // Copy and commit logic would go here if not exceeding 32 lines.
    }
    fs::path ts = custom_test_script; if (ts.empty()) {
        fs::path gh = cfg.spip_root / "scripts" / "generate_test.py";
        std::string code = get_exec_output(std::format("python3 {} {}", quote_arg(gh.string()), quote_arg(pkg)));
        ts = fs::current_path() / ("test_" + pkg + "_gen.py"); std::ofstream os(ts); os << code; os.close();
    }
    std::vector<std::string> to_do = select_versions(vary_python, revision_limit, test_all_revisions, pkg_revision_limit, pinned_pkg_ver);
    parallel_execution(to_do, ts, python_version, profile, no_cleanup, vary_python);
    summarize(profile);
}

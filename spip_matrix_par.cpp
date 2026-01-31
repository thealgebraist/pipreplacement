#include "spip_matrix_tester.h"
#include "spip_env.h"
#include "spip_install.h"
#include "spip_test.h"

void MatrixTester::parallel_execution(const std::vector<std::string>& to_do, const fs::path& ts, const std::string& pv, bool prof, bool nc, bool vp) {
    unsigned int nt = cfg.concurrency ? cfg.concurrency : 4;
    std::atomic<size_t> idx{0};
    std::mutex m_res;
    std::vector<std::thread> ws;

    auto wt = [&]() {
        while(!g_interrupted) {
            size_t ti = idx.fetch_add(1);
            if(ti >= to_do.size()) break;
            const auto& ver = to_do[ti];
            
            // 1. Setup isolated environment
            Config tcfg = cfg;
            std::string py_ver = vp ? (ver.find(':') != std::string::npos ? split(ver, ':')[0] : pv) : pv;
            std::string pkg_ver = vp ? (ver.find(':') != std::string::npos ? split(ver, ':')[1] : ver) : ver;
            
            // Unique path per task to avoid collisions
            tcfg.project_env_path = cfg.envs_root / ("mat_" + pkg + "_" + pkg_ver + "_" + std::to_string(ti));
            
            // Use a unique branch name for worktree
            tcfg.project_hash += "_" + std::to_string(ti); 
            
            setup_project_env(tcfg, py_ver);

            // 2. Install
            ResourceUsage stats = {};
            std::unique_ptr<ResourceProfiler> profiler;
            if (prof) profiler = std::make_unique<ResourceProfiler>(tcfg.project_env_path);
            
            bool install_ok = resolve_and_install(tcfg, {pkg}, pkg_ver, py_ver);
            
            // 3. Tests
            bool pkg_ok = false;
            bool custom_ok = false;
            
            if (install_ok) {
                // Inline package test logic to capture result
                const fs::path sp = get_site_packages(tcfg);
                if (!sp.empty()) {
                   fs::path bin = tcfg.project_env_path / "bin" / "python";
                   std::string check = std::format("{} -c \"import {}; print('OK')\"", quote_arg(bin.string()), pkg);
                   if (run_shell(check.c_str()) == 0) {
                        std::string test_cmd = std::format("{} -m pytest {}", quote_arg(bin.string()), quote_arg(sp.string()));
                        pkg_ok = (run_shell(test_cmd.c_str()) == 0);
                   }
                }
                
                if (!ts.empty()) {
                    std::string cmd = std::format("{} {}", quote_arg(tcfg.project_env_path / "bin" / "python"), quote_arg(ts.string()));
                    custom_ok = (run_shell(cmd.c_str()) == 0);
                }
            }
            
            if (profiler) stats = profiler->stop();
            
            // 4. Cleanup
            if (!nc) {
                // Detach worktree
                run_shell(std::format("cd {} && git worktree remove --force {}", quote_arg(cfg.repo_path.string()), quote_arg(tcfg.project_env_path.string())).c_str());
                run_shell(std::format("rm -rf {}", quote_arg(tcfg.project_env_path.string())).c_str());
            }

            // 5. Record
            {
                std::lock_guard<std::mutex> l(m_res);
                results.push_back({ver, install_ok, pkg_ok, custom_ok, stats});
                std::cout << "." << std::flush;
            }
        }
    };
    
    for(unsigned int i=0; i<nt; ++i) ws.emplace_back(wt);
    for(auto& t : ws) t.join();
    std::cout << std::endl;
}
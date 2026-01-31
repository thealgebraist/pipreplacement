#include "spip_matrix_tester.h"

void MatrixTester::parallel_execution(const std::vector<std::string>& to_do, const fs::path& ts, const std::string& pv, bool prof, bool nc, bool vp) {
    (void)ts; (void)pv; (void)prof; (void)nc; (void)vp;
    unsigned int nt = cfg.concurrency ? cfg.concurrency : 4; std::atomic<size_t> idx{0}; std::mutex m_out, m_res, m_base, m_git; std::vector<std::thread> ws;
    ErrorKnowledgeBase kb(cfg.db_file);
    auto wt = [&]() {
        while(!g_interrupted) {
            size_t ti = idx.fetch_add(1); if(ti >= to_do.size()) break;
            const auto& ver = to_do[ti]; (void)ver; // actual worker logic needed
        }
    };
    for(unsigned int i=0; i<nt; ++i) ws.emplace_back(wt);
    for(auto& t : ws) t.join();
}
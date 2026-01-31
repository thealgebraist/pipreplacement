#include "spip_distributed.h"
#include "spip_db.h"
#include "spip_env.h"

void init_queue_db(const Config& cfg) {
    sqlite3* db; sqlite3_open((cfg.spip_root / "queue.db").c_str(), &db); sqlite3_busy_timeout(db, 10000);
    sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS work_queue (id INTEGER PRIMARY KEY, pkg_name TEXT, pkg_ver TEXT, py_ver TEXT, status TEXT, worker_id TEXT, result_json TEXT, started_at REAL, finished_at REAL);", nullptr, nullptr, nullptr);
    sqlite3_close(db);
}

void run_master(const Config& cfg, const std::vector<std::string>& args) {
    if (args.size() < 2) return; std::string pkg = args[1]; int limit = -1;
    for (size_t i = 2; i < args.size(); ++i) if (args[i] == "--limit" && i + 1 < args.size()) limit = std::stoi(args[++i]);
    setup_project_env(const_cast<Config&>(cfg)); init_queue_db(cfg);
    auto versions = get_all_versions(pkg); std::vector<std::string> py_v = {"3.7", "3.8", "3.9", "3.10", "3.11", "3.12", "3.13"};
    sqlite3* db; sqlite3_open((cfg.spip_root / "queue.db").c_str(), &db); sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    int count = 0; for (const auto& v : versions) {
        if (limit > 0 && count++ >= limit) break;
        for (const auto& py : py_v) sqlite3_exec(db, std::format("INSERT INTO work_queue (pkg_name, pkg_ver, py_ver, status) VALUES ({}, {}, {}, 'PENDING');", quote_arg(pkg), quote_arg(v), quote_arg(py)).c_str(), nullptr, nullptr, nullptr);
    }
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr); sqlite3_close(db);
}

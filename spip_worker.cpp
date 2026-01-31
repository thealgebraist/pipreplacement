#include "spip_distributed.h"
#include "spip_env.h"
#include "spip_matrix.h"

void run_worker(Config& cfg) {
    setup_project_env(cfg); init_queue_db(cfg);
    std::cout << CYAN << "👷 SPIP Worker [" << cfg.worker_id << "] started." << RESET << std::endl;
    sqlite3* db; sqlite3_open((cfg.spip_root / "queue.db").c_str(), &db); sqlite3_busy_timeout(db, 10000);
    while (!g_interrupted) {
        sqlite3_stmt* stmt; const char* sql = "UPDATE work_queue SET status='CLAIMED', worker_id=?, started_at=julianday('now') WHERE id = (SELECT id FROM work_queue WHERE status='PENDING' LIMIT 1) RETURNING id, pkg_name, pkg_ver, py_ver;";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        sqlite3_bind_text(stmt, 1, cfg.worker_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            int tid = sqlite3_column_int(stmt, 0); std::string pkg = (const char*)sqlite3_column_text(stmt, 1);
            std::string ver = (const char*)sqlite3_column_text(stmt, 2); std::string py = (const char*)sqlite3_column_text(stmt, 3);
            sqlite3_finalize(stmt); std::cout << YELLOW << "⚡ Task [" << tid << "]: " << pkg << RESET << std::endl;
            try {
                Config w_cfg = cfg; w_cfg.concurrency = 1; w_cfg.telemetry = true;
                matrix_test(w_cfg, pkg, "", py, false, false, 1, false, false, 1, ver);
                sqlite3_exec(db, std::format("UPDATE work_queue SET status='COMPLETED', finished_at=julianday('now') WHERE id={};", tid).c_str(), nullptr, nullptr, nullptr);
            } catch (...) { sqlite3_exec(db, std::format("UPDATE work_queue SET status='FAILED', finished_at=julianday('now') WHERE id={};", tid).c_str(), nullptr, nullptr, nullptr); }
        } else { sqlite3_finalize(stmt); std::this_thread::sleep_for(std::chrono::seconds(2)); }
    }
    sqlite3_close(db);
}

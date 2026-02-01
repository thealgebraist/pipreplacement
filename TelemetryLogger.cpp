#include "TelemetryLogger.h"

TelemetryLogger::TelemetryLogger(const Config& c, const std::string& id)
    : cfg(c), test_id(id), last_user_vec(MAX_CORES, 0), last_sys_vec(MAX_CORES, 0), last_io_vec(MAX_CORES, 0)
{
    fs::path db_dir = cfg.spip_root / "telemetry";
    std::error_code ec;
    if (!fs::exists(db_dir, ec)) fs::create_directories(db_dir, ec);
    fs::path db_path = db_dir / ("telemetry_" + test_id + ".db");
    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ Failed to open telemetry database: " << db_path << std::endl;
        return;
    }
    sqlite3_busy_timeout(db, 10000);
    const char* sql = "CREATE TABLE IF NOT EXISTS telemetry ("
                      "test_id TEXT, timestamp REAL, core_id INTEGER, cpu_user REAL, cpu_sys REAL, "
                      "mem_kb INTEGER, net_in INTEGER, net_out INTEGER, disk_read INTEGER, disk_write INTEGER, "
                      "iowait REAL);";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    const char* status_sql = "CREATE TABLE IF NOT EXISTS test_run_status ("
                            "test_id TEXT PRIMARY KEY, status TEXT, error_msg TEXT);";
    sqlite3_exec(db, status_sql, nullptr, nullptr, nullptr);
    const char* insert_sql = "INSERT INTO telemetry VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr);
    const char* status_insert_sql = "INSERT OR REPLACE INTO test_run_status VALUES (?, ?, ?);";
    sqlite3_prepare_v2(db, status_insert_sql, -1, &status_stmt, nullptr);
}

TelemetryLogger::~TelemetryLogger() {
    if (insert_stmt) sqlite3_finalize(insert_stmt);
    if (status_stmt) sqlite3_finalize(status_stmt);
    if (db) sqlite3_close(db);
}


    if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
        std::cerr << "❌ Failed to open telemetry database: " << db_path << std::endl;
        return;
    }
    sqlite3_busy_timeout(db, 10000);
    const char* sql = "CREATE TABLE IF NOT EXISTS telemetry ("
                      "test_id TEXT, timestamp REAL, core_id INTEGER, cpu_user REAL, cpu_sys REAL, "
                      "mem_kb INTEGER, net_in INTEGER, net_out INTEGER, disk_read INTEGER, disk_write INTEGER, "
                      "iowait REAL);";
    sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    const char* status_sql = "CREATE TABLE IF NOT EXISTS test_run_status ("
                            "test_id TEXT PRIMARY KEY, status TEXT, error_msg TEXT);";
    sqlite3_exec(db, status_sql, nullptr, nullptr, nullptr);
    const char* insert_sql = "INSERT INTO telemetry VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr);
    const char* status_insert_sql = "INSERT OR REPLACE INTO test_run_status VALUES (?, ?, ?);";
    sqlite3_prepare_v2(db, status_insert_sql, -1, &status_stmt, nullptr);
}

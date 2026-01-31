#include "TelemetryLogger.h"
void TelemetryLogger::log_test_run_status(const std::string& status, const std::string& error_msg) {
    if (!status_stmt) return;
    sqlite3_reset(status_stmt);
    sqlite3_bind_text(status_stmt, 1, test_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(status_stmt, 2, status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(status_stmt, 3, error_msg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(status_stmt);
}

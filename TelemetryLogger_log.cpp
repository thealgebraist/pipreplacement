#include "TelemetryLogger.h"

void TelemetryLogger::log_to_db(double ts, int core, double u, double s, long mem, long ni, long no, long dr, long dw, double wait) {
    if (!insert_stmt) return;
    sqlite3_reset(insert_stmt);
    sqlite3_bind_text(insert_stmt, 1, test_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(insert_stmt, 2, ts);
    sqlite3_bind_int(insert_stmt, 3, core);
    sqlite3_bind_double(insert_stmt, 4, u);
    sqlite3_bind_double(insert_stmt, 5, s);
    sqlite3_bind_int64(insert_stmt, 6, mem);
    sqlite3_bind_int64(insert_stmt, 7, ni);
    sqlite3_bind_int64(insert_stmt, 8, no);
    sqlite3_bind_int64(insert_stmt, 9, dr);
    sqlite3_bind_int64(insert_stmt, 10, dw);
    sqlite3_bind_double(insert_stmt, 11, wait);
    sqlite3_step(insert_stmt);
}

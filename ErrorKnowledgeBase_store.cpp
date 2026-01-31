#include "ErrorKnowledgeBase.h"

void ErrorKnowledgeBase::store(const std::string& pkg, const std::string& py_ver, const std::string& exc, const std::string& fix) {
    if (!db) return;
    std::lock_guard<std::mutex> lock(m_db);
    const char* sql = "INSERT INTO exceptions (package, python_version, exception_text, suggested_fix) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, pkg.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, py_ver.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, exc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, fix.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

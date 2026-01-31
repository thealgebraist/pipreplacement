#include "ErrorKnowledgeBase.h"

std::vector<std::pair<std::string, std::string>> ErrorKnowledgeBase::get_fixes_for_pkg(const std::string& pkg) {
    std::vector<std::pair<std::string, std::string>> fixes;
    if (!db) return fixes;
    std::lock_guard<std::mutex> lock(m_db);
    const char* sql = "SELECT exception_text, suggested_fix FROM exceptions WHERE package = ? AND suggested_fix != '';";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, pkg.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        fixes.push_back({ reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)) });
    }
    sqlite3_finalize(stmt);
    return fixes;
}

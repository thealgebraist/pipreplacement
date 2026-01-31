#include "ErrorKnowledgeBase.h"

std::string ErrorKnowledgeBase::lookup_fix(const std::string& exc) {
    if (!db) return "";
    std::lock_guard<std::mutex> lock(m_db);
    const char* sql = "SELECT suggested_fix FROM exceptions WHERE (exception_text = ? OR ? LIKE '%' || exception_text || '%') AND suggested_fix != '' ORDER BY (length(exception_text)) DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, exc.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, exc.c_str(), -1, SQLITE_TRANSIENT);
    std::string fix = "";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) fix = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    return fix;
}

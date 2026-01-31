#include "ErrorKnowledgeBase.h"

ErrorKnowledgeBase::ErrorKnowledgeBase(const fs::path& db_path) {
    if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
        std::cerr << RED << "❌ Error opening knowledge base: " << sqlite3_errmsg(db) << RESET << std::endl;
    } else {
        const char* sql = "CREATE TABLE IF NOT EXISTS exceptions ("
                          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                          "package TEXT, python_version TEXT, exception_text TEXT,"
                          "suggested_fix TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
        char* err_msg = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
            std::cerr << RED << "❌ Error creating table: " << err_msg << RESET << std::endl;
            sqlite3_free(err_msg);
        }
    }
}

ErrorKnowledgeBase::~ErrorKnowledgeBase() { if(db) sqlite3_close(db); }

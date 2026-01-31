#pragma once
#include "spip_utils.h"

class ErrorKnowledgeBase {
    sqlite3* db = nullptr;
    std::mutex m_db;
public:
    ErrorKnowledgeBase(const fs::path& db_path);
    ~ErrorKnowledgeBase();
    void store(const std::string& pkg, const std::string& py_ver, const std::string& exc, const std::string& fix = "");
    std::string lookup_fix(const std::string& exc);
    std::vector<std::pair<std::string, std::string>> get_fixes_for_pkg(const std::string& pkg);
};

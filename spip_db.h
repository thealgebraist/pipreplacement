#pragma once
#include "spip_utils.h"

void init_db();
fs::path get_db_path(const std::string& pkg);
void fetch_package_metadata(const Config& cfg, const std::string& pkg);
void db_worker(std::queue<std::string>& q, std::mutex& m, std::atomic<int>& count, int total, Config cfg);
std::string extract_field(const std::string& json, const std::string& key);
std::vector<std::string> extract_array(const std::string& json, const std::string& key);
std::vector<std::string> get_all_versions(const std::string& pkg);

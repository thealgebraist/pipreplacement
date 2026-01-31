#pragma once
#include "spip_utils.h"

int score_wheel(const std::string& url, const std::string& target_py = "3.12");
fs::path get_cached_wheel_path(const Config& cfg, const PackageInfo& info);
PackageInfo get_package_info(const std::string& pkg, const std::string& version = "", const std::string& target_py = "3.12");
bool resolve_and_install(const Config& cfg, const std::vector<std::string>& targets, const std::string& version = "", const std::string& target_py = "3.12");
void uninstall_package(const Config& cfg, const std::string& pkg);
void prune_orphans(const Config& cfg);
void record_manual_install(const Config& cfg, const std::string& pkg, bool add);
fs::path get_site_packages(const Config& cfg);

#pragma once
#include "spip_utils.h"

void print_tree(const std::string& pkg, int depth, std::set<std::string>& visited);
void run_package_tests(const Config& cfg, const std::string& pkg);
void run_all_package_tests(const Config& cfg);
void boot_environment(const Config& cfg, const std::string& script_path);
void freeze_environment(const Config& cfg, const std::string& output_file);
void audit_environment(const Config& cfg);
void review_code(const Config& cfg);
void verify_environment(const Config& cfg);
void trim_environment(const Config& cfg, const std::string& script_path);
std::string extract_exception(const std::string& output);

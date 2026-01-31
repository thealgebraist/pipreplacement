#pragma once
#include "spip_utils.h"

void matrix_test(const Config& cfg, const std::string& pkg, const std::string& custom_test_script = "", const std::string& python_version = "auto", bool profile = false, bool no_cleanup = false, int revision_limit = -1, bool test_all_revisions = false, bool vary_python = false, int pkg_revision_limit = 1, const std::string& pinned_pkg_ver = "");
void parallel_download(const Config& cfg, const std::vector<PackageInfo>& info_list);
int benchmark_concurrency(const Config& cfg);
void run_thread_test(const Config& cfg, int num_threads = -1);
std::map<std::string, PackageInfo> resolve_only(const std::vector<std::string>& targets, const std::string& version = "", const std::string& target_py = "3.12");

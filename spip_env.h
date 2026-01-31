#pragma once
#include "spip_utils.h"

Config init_config();
void ensure_scripts(const Config& cfg);
void ensure_envs_tmpfs(const Config& cfg);
void ensure_dirs(const Config& cfg);
bool branch_exists(const Config& cfg, const std::string& branch);
void setup_project_env(Config& cfg, const std::string& version = "3");
void commit_state(const Config& cfg, const std::string& msg);
void exec_with_setup(Config& cfg, std::function<void(Config&)> func);
bool require_args(const std::vector<std::string>& args, size_t min_count, const std::string& usage_msg);

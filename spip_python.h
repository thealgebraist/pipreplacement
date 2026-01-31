#pragma once
#include "spip_utils.h"

std::string get_platform_tuple();
std::string get_full_version_map(const std::string& short_ver);
std::string ensure_python_bin(const Config& cfg, const std::string& version);
void create_base_version(const Config& cfg, const std::string& version);
std::string parse_python_requirement(const std::string& req);

#pragma once
#include "spip_utils.h"

void run_command(Config& cfg, const std::vector<std::string>& args);
void run_command_top(Config& cfg, const std::vector<std::string>& args);
void run_command_install(Config& cfg, const std::vector<std::string>& args);
void run_command_uninstall(Config& cfg, const std::vector<std::string>& args);
void run_command_maintenance(Config& cfg, const std::vector<std::string>& args);
void run_command_matrix(Config& cfg, const std::vector<std::string>& args);
int cmd_diff(int argc, char** argv);
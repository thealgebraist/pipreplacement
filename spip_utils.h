#pragma once
#include "spip_types.h"

std::string compute_hash(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string quote_arg(const std::string& arg);
int run_shell(const char* cmd);
std::string get_exec_output(const std::string& cmd);
const std::string RESET = "\033[0m";
const std::string BOLD = "\033[1m";
const std::string CYAN = "\033[36m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";
const std::string MAGENTA = "\033[35m";
const std::string RED = "\033[31m";

#pragma once
#include "spip_types.h"

std::string compute_hash(const std::string& s);
std::vector<std::string> split(const std::string& s, char delim);
std::string quote_arg(const std::string& arg);
int run_shell(const char* cmd);
std::string get_exec_output(const std::string& cmd);

constexpr std::string_view RESET = "\033[0m";
constexpr std::string_view BOLD = "\033[1m";
constexpr std::string_view CYAN = "\033[36m";
constexpr std::string_view GREEN = "\033[32m";
constexpr std::string_view YELLOW = "\033[33m";
constexpr std::string_view BLUE = "\033[34m";
constexpr std::string_view MAGENTA = "\033[35m";
constexpr std::string_view RED = "\033[31m";
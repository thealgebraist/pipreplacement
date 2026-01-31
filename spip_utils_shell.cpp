#include "spip_utils.h"

std::string quote_arg(const std::string& arg) {
    std::string result = "\"";
    for (char c : arg) {
        if (c == '"' || c == '\\' || c == '$' || c == '`') result += "\\";
        result += c;
    }
    result += "\"";
    return result;
}

int run_shell(const char* cmd) {
    return std::system(cmd);
}


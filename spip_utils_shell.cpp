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
    if (g_interrupted) return 130;
    int ret = std::system(cmd);
    if ((WIFSIGNALED(ret) && WTERMSIG(ret) == SIGINT) || 
        (WIFEXITED(ret) && WEXITSTATUS(ret) == 130)) {
        g_interrupted = true;
    }
    return ret;
}


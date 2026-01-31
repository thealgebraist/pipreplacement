#include "spip_python.h"

std::string get_platform_tuple() {
#ifdef __APPLE__
    #if defined(__aarch64__)
        return "aarch64-apple-darwin";
    #else
        return "x86_64-apple-darwin";
    #endif
#elif defined(__linux__)
    return "x86_64-unknown-linux-gnu";
#else
    return "unknown";
#endif
}

std::string get_full_version_map(const std::string& short_ver) {
    if (short_ver == "3.13") return "3.13.0";
    if (short_ver == "3.12") return "3.12.7";
    if (short_ver == "3.11") return "3.11.9";
    if (short_ver == "3.10") return "3.10.16";
    if (short_ver == "3.9") return "3.9.21";
    if (short_ver == "3.8") return "3.8.20";
    if (short_ver == "3.7") return "3.7.17";
    if (short_ver == "2.7") return "2.7.18";
    return short_ver + ".0";
}

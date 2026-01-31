#include "spip_utils.h"

std::string compute_hash(const std::string& s) {
    uint64_t h = 0xcbf29ce484222325;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001b3;
    }
    std::stringstream ss; ss << std::hex << h;
    return ss.str().substr(0, 16);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) parts.push_back(item);
    return parts;
}

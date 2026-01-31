#include "spip_db.h"

std::string extract_field(const std::string& json, const std::string& key) {
    std::regex re(\"" + key + ":\\s*\"([^\"]*?)\""); std::smatch match;
    if (std::regex_search(json, match, re)) return match[1];
    return "";
}

std::vector<std::string> extract_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result; std::regex re(\"" + key + ":\\s*\[(.*?)\]"); std::smatch match;
    if (std::regex_search(json, match, re)) {
        std::string array_content = match[1]; std::regex item_re(\"([^\"]*?)\");
        auto begin = std::sregex_iterator(array_content.begin(), array_content.end(), item_re);
        auto end = std::sregex_iterator();
        for (std::sregex_iterator i = begin; i != end; ++i) result.push_back((*i)[1]);
    }
    return result;
}


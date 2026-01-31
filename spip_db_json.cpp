#include "spip_db.h"

std::string extract_field(const std::string& json, const std::string& key) {
    (void)json;
    const std::string pattern = "\"" + key + R"(\"\:\s*\"([^\"]*?)\")";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re)) return match[1];
    return "";
}

std::vector<std::string> extract_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    const std::string pattern = "\"" + key + R"(\"\:\s*\[(.*?)(\s*\])?)";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re)) {
        const std::string array_content = match[1];
        std::regex item_re(R"("[^"]*?")");
        auto begin = std::sregex_iterator(array_content.begin(), array_content.end(), item_re);
        auto end = std::sregex_iterator();
        for (std::sregex_iterator i = begin; i != end; ++i) {
            std::string s = (*i).str();
            if (s.length() >= 2) result.push_back(s.substr(1, s.length() - 2));
        }
    }
    return result;
}

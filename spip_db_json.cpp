#include "spip_db.h"
#include <regex>

std::string extract_field(const std::string& json, const std::string& key) {
    // Regex to match "key": "value" or "key": literal
    std::regex re("\"" + key + "\":\\s*\"(.*?)\"");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match.str(1);
    }
    // Try without quotes for numbers/booleans
    std::regex re2("\"" + key + "\":\\s*([^,}]+)");
    if (std::regex_search(json, match, re2) && match.size() > 1) {
        return match.str(1);
    }
    return "";
}

std::vector<std::string> extract_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::regex re("\"" + key + "\":\\s*\\[(.*?)\\]");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        std::string content = match.str(1);
        std::regex item_re("\"([^\"]+)\"");
        std::sregex_iterator begin(content.begin(), content.end(), item_re);
        std::sregex_iterator end;
        for (std::sregex_iterator i = begin; i != end; ++i) {
            result.push_back(i->str(1));
        }
    }
    return result;
}
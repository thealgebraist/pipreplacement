#include "spip_db.h"

std::string extract_field(const std::string& json, const std::string& key) {
    std::string pattern = std::string("\"") + key + R
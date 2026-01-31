#include "spip_db.h"

std::string extract_field(const std::string& json, const std::string& key) {
    std::regex re(""" + key + R
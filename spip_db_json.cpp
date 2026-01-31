#include "spip_db.h"

std::string extract_field(const std::string& json, const std::string& key) {
    const std::string pattern = """ + key + R
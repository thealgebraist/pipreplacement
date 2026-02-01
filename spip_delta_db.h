#ifndef SPIP_DELTA_DB_H
#define SPIP_DELTA_DB_H
#include <string>
#include <optional>

struct DeltaRecord {
    std::string package_name;
    std::string source_version;
    std::string target_version;
    size_t delta_size;
    size_t target_size;
    double similarity;
    std::string delta_path;
    std::string source_url;
    std::string target_url;
    time_t created_at;
};

void init_delta_db();
void store_delta(const DeltaRecord& record);
std::optional<DeltaRecord> query_delta(const std::string& package, 
                                       const std::string& from_version,
                                       const std::string& to_version);
bool is_delta_beneficial(const DeltaRecord& record);
void cleanup_old_deltas(size_t max_size_mb);

#endif

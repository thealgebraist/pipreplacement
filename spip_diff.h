#ifndef SPIP_DIFF_H
#define SPIP_DIFF_H
#include <string>
#include <vector>

struct VersionDiff {
    std::string version;
    std::string wheel_path;
    size_t wheel_size;
};

struct DiffResult {
    std::string version_a;
    std::string version_b;
    size_t delta_size;
    double similarity;
};

std::vector<VersionDiff> fetch_versions(const std::string& package, int limit);
DiffResult compute_vcdiff(const std::string& path_a, const std::string& path_b);
void print_diff_matrix(const std::vector<VersionDiff>& versions, const std::vector<DiffResult>& results);

#endif

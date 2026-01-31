#pragma once
#include "spip_utils.h"

class ResourceProfiler {
    std::chrono::time_point<std::chrono::steady_clock> start_wall;
    struct rusage start_usage;
    intmax_t start_disk;
    fs::path track_path;
    bool active = false;

public:
    ResourceProfiler(fs::path p = "");
    ResourceUsage stop();
};

uintmax_t get_dir_size(const fs::path& p);

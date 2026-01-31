#include "ResourceProfiler.h"

ResourceProfiler::ResourceProfiler(fs::path p) : track_path(p) {
    start_wall = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &start_usage);
    std::error_code ec;
    if (!track_path.empty() && fs::exists(track_path, ec)) {
        start_disk = static_cast<intmax_t>(get_dir_size(track_path));
        active = true;
    } else {
        start_disk = 0;
    }
}

ResourceUsage ResourceProfiler::stop() {
    auto end_wall = std::chrono::steady_clock::now();
    struct rusage end_usage;
    getrusage(RUSAGE_SELF, &end_usage);
    intmax_t end_disk = 0;
    std::error_code ec;
    if (active && !track_path.empty() && fs::exists(track_path, ec)) {
         end_disk = static_cast<intmax_t>(get_dir_size(track_path));
    }
    double user_time = (end_usage.ru_utime.tv_sec - start_usage.ru_utime.tv_sec) +
                       (end_usage.ru_utime.tv_usec - start_usage.ru_utime.tv_usec) / 1e6;
    double sys_time = (end_usage.ru_stime.tv_sec - start_usage.ru_stime.tv_sec) +
                      (end_usage.ru_stime.tv_usec - start_usage.ru_stime.tv_usec) / 1e6;
    std::chrono::duration<double> wall_diff = end_wall - start_wall;
    return { user_time + sys_time, end_usage.ru_maxrss, wall_diff.count(), end_disk - start_disk };
}

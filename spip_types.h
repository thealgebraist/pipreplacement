#pragma once
#include "spip_common.h"

struct ResourceUsage {
    double cpu_time_seconds;
    long peak_memory_kb;
    double wall_time_seconds;
    intmax_t disk_delta_bytes;
};

struct Config {
    fs::path home_dir;
    fs::path spip_root;
    fs::path repo_path;
    fs::path envs_root;
    fs::path current_project;
    std::string project_hash;
    fs::path project_env_path;
    fs::path db_file;
    std::string pypi_mirror = "https://pypi.org";
    int concurrency = std::thread::hardware_concurrency();
    bool telemetry = false;
    std::string worker_id = "worker_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count() % 10000);
};

struct PackageInfo {
    std::string name;
    std::string version;
    std::string wheel_url;
    std::string requires_python;
    std::vector<std::string> dependencies;
};

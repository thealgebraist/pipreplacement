#pragma once
#include "spip_utils.h"

class TelemetryLogger {
    const Config& cfg;
    std::string test_id;
    std::atomic<bool> running{false};
    std::thread worker;
    sqlite3* db = nullptr;
    sqlite3_stmt* insert_stmt = nullptr;
    const size_t MAX_CORES = 1024;
    std::vector<uint64_t> last_user_vec, last_sys_vec, last_io_vec;
    uint64_t last_net_in = 0, last_net_out = 0;

public:
    TelemetryLogger(const Config& c, const std::string& id);
    ~TelemetryLogger();
    void start();
    void stop();
private:
    void loop();
    void sample();
    void log_to_db(double ts, int core, double u, double s, long mem, long ni, long no, long dr, long dw, double wait);
};

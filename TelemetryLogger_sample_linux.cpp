#include "TelemetryLogger.h"
#ifdef __linux__
void TelemetryLogger::sample() {
    double ts = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::ifstream stat("/proc/stat"); std::string line;
    while (std::getline(stat, line) && line.starts_with("cpu")) {
        if (line.starts_with("cpu ")) continue;
        std::stringstream ss(line); std::string cpu_label; ss >> cpu_label;
        int core_id = std::stoi(cpu_label.substr(3)); if (core_id >= (int)MAX_CORES) break;
        uint64_t u, n, s, id, io, irq, soft; if (!(ss >> u >> n >> s >> id >> io >> irq >> soft)) continue;
        double du = (u + n > last_user_vec[core_id]) ? (double)(u + n - last_user_vec[core_id]) : 0;
        double ds = (s + irq + soft > last_sys_vec[core_id]) ? (double)(s + irq + soft - last_sys_vec[core_id]) : 0;
        double dio = (io > last_io_vec[core_id]) ? (double)(io - last_io_vec[core_id]) : 0;
        last_user_vec[core_id] = u + n; last_sys_vec[core_id] = s + irq + soft; last_io_vec[core_id] = io;
        log_to_db(ts, core_id, du, ds, 0, 0, 0, 0, 0, dio);
    }
    std::ifstream meminfo("/proc/meminfo"); long total = 0, free = 0, cached = 0, buffers = 0;
    while (std::getline(meminfo, line)) {
        if (line.starts_with("MemTotal:")) total = std::stol(line.substr(10));
        else if (line.starts_with("MemFree:")) free = std::stol(line.substr(10));
        else if (line.starts_with("Cached:")) cached = std::stol(line.substr(10));
        else if (line.starts_with("Buffers:")) buffers = std::stol(line.substr(10));
    }
    log_to_db(ts, -1, 0, 0, total - free - cached - buffers, 0, 0, 0, 0, 0);
    std::ifstream netdev("/proc/net/dev"); uint64_t rb = 0, tb = 0;
    std::getline(netdev, line); std::getline(netdev, line);
    while (std::getline(netdev, line)) {
        size_t colon = line.find(':'); if (colon == std::string::npos) continue;
        std::stringstream ss(line.substr(colon + 1)); uint64_t r, dummy;
        ss >> r >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy;
        rb += r; ss >> r; tb += r;
    }
    log_to_db(ts, -2, 0, 0, 0, (long)(rb - last_net_in), (long)(tb - last_net_out), 0, 0, 0);
    last_net_in = rb; last_net_out = tb;
}
#endif

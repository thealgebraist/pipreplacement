#include "TelemetryLogger.h"
#ifdef __APPLE__
void TelemetryLogger::sample() {
    double ts = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    natural_t cpuCount; processor_info_array_t infoArray; mach_msg_type_number_t infoCount;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &infoArray, &infoCount) == KERN_SUCCESS) {
        for (natural_t i = 0; i < cpuCount && i < MAX_CORES; ++i) {
            uint64_t u = infoArray[i * CPU_STATE_MAX + CPU_STATE_USER];
            uint64_t s = infoArray[i * CPU_STATE_MAX + CPU_STATE_SYSTEM];
            double du = (u > last_user_vec[i]) ? (double)(u - last_user_vec[i]) : 0;
            double ds = (s > last_sys_vec[i]) ? (double)(s - last_sys_vec[i]) : 0;
            last_user_vec[i] = u; last_sys_vec[i] = s;
            log_to_db(ts, (int)i, du, ds, 0, 0, 0, 0, 0, 0); 
        }
        vm_deallocate(mach_task_self(), (vm_address_t)infoArray, infoCount * sizeof(int));
    }
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT; vm_statistics_data_t vm_stats;
    if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm_stats, &count) == KERN_SUCCESS) {
        log_to_db(ts, -1, 0, 0, (vm_stats.active_count + vm_stats.wire_count) * (vm_page_size / 1024), 0, 0, 0, 0, 0);
    }
    struct ifaddrs *ifa_list = nullptr, *ifa;
    if (getifaddrs(&ifa_list) == 0) {
        uint64_t ibytes = 0, obytes = 0;
        for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr->sa_family == AF_LINK) {
                struct if_data *ifd = (struct if_data *)ifa->ifa_data;
                ibytes += ifd->ifi_ibytes; obytes += ifd->ifi_obytes;
            }
        }
        log_to_db(ts, -2, 0, 0, 0, (long)(ibytes - last_net_in), (long)(obytes - last_net_out), 0, 0, 0);
        last_net_in = ibytes; last_net_out = obytes;
        freeifaddrs(ifa_list);
    }
}
#endif

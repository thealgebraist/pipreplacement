#include "TelemetryLogger.h"
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#include <sys/sysctl.h>
#include <libproc.h>

void TelemetryLogger::sample() {
    double ts = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Collect process count
    int num_procs = 0;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL, 0};
    size_t size = 0;
    if (sysctl(mib, 4, nullptr, &size, nullptr, 0) == 0) {
        num_procs = size / sizeof(struct kinfo_proc);
    }
    
    // Collect open file descriptor count for current process
    int open_fds = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, nullptr, 0);
    if (open_fds > 0) open_fds = open_fds / sizeof(struct proc_fdinfo);
    
    natural_t cpuCount; processor_info_array_t infoArray; mach_msg_type_number_t infoCount;
    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &infoArray, &infoCount) == KERN_SUCCESS) {
        for (natural_t i = 0; i < cpuCount && i < MAX_CORES; ++i) {
            uint64_t u = infoArray[i * CPU_STATE_MAX + CPU_STATE_USER];
            uint64_t s = infoArray[i * CPU_STATE_MAX + CPU_STATE_SYSTEM];
            double du = (u > last_user_vec[i]) ? (double)(u - last_user_vec[i]) : 0;
            double ds = (s > last_sys_vec[i]) ? (double)(s - last_sys_vec[i]) : 0;
            last_user_vec[i] = u; last_sys_vec[i] = s;
            log_to_db(ts, (int)i, du, ds, 0, 0, 0, 0, 0, 0, num_procs, open_fds); 
        }
        vm_deallocate(mach_task_self(), (vm_address_t)infoArray, infoCount * sizeof(int));
    }
    mach_msg_type_number_t count = HOST_VM_INFO_COUNT; vm_statistics_data_t vm_stats;
    if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm_stats, &count) == KERN_SUCCESS) {
        log_to_db(ts, -1, 0, 0, (vm_stats.active_count + vm_stats.wire_count) * (vm_page_size / 1024), 0, 0, 0, 0, 0, num_procs, open_fds);
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
        log_to_db(ts, -2, 0, 0, 0, (long)(ibytes - last_net_in), (long)(obytes - last_net_out), 0, 0, 0, num_procs, open_fds);
        last_net_in = ibytes; last_net_out = obytes;
        freeifaddrs(ifa_list);
    }
}
#endif
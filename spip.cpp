#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <format>
#include <functional>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <algorithm>
#include <regex>
#include <set>
#include <map>
#include <chrono>
#include <string_view>
#include <sys/resource.h>
#include <csignal>
#include <sqlite3.h>
#include <future>
#include <semaphore>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/mach_host.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <net/if_dl.h>
#endif

// Semaphores for resource capping
std::counting_semaphore<8> g_git_sem{8}; 

volatile std::atomic<bool> g_interrupted{false};

void signal_handler(int) {
    g_interrupted = true;
}

namespace fs = std::filesystem;

uintmax_t get_dir_size(const fs::path& p);

struct ResourceUsage {
    double cpu_time_seconds;
    long peak_memory_kb;
    double wall_time_seconds;
    intmax_t disk_delta_bytes;
};

class ResourceProfiler {
    std::chrono::time_point<std::chrono::steady_clock> start_wall;
    struct rusage start_usage;
    intmax_t start_disk;
    fs::path track_path;
    bool active = false;

public:
    ResourceProfiler(fs::path p = "") : track_path(p) {
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

    ResourceUsage stop() {
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

        return {
            user_time + sys_time,
            end_usage.ru_maxrss, 
            wall_diff.count(),
            end_disk - start_disk
        };
    }
};

const std::string RESET = "\033[0m";
const std::string BOLD = "\033[1m";
const std::string CYAN = "\033[36m";
const std::string GREEN = "\033[32m";
const std::string YELLOW = "\033[33m";
const std::string BLUE = "\033[34m";
const std::string MAGENTA = "\033[35m";
const std::string RED = "\033[31m";

struct Config {
    	fs::path home_dir;
		fs::path spip_root;
		fs::path repo_path;
		fs::path envs_root;
		fs::path current_project;
		std::string project_hash;
		fs::path project_env_path;
        fs::path db_file;
		std::string pypi_mirror = "https://pypi.org"; // Default
        int concurrency = std::thread::hardware_concurrency(); 
        bool telemetry = false;
};

class ErrorKnowledgeBase {
    sqlite3* db = nullptr;
    std::mutex m_db;
public:
    ErrorKnowledgeBase(const fs::path& db_path) {
        if (sqlite3_open(db_path.string().c_str(), &db) != SQLITE_OK) {
            std::cerr << RED << "❌ Error opening knowledge base: " << sqlite3_errmsg(db) << RESET << std::endl;
        } else {
            const char* sql = "CREATE TABLE IF NOT EXISTS exceptions ("
                              "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                              "package TEXT,"
                              "python_version TEXT,"
                              "exception_text TEXT,"
                              "suggested_fix TEXT,"
                              "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";
            char* err_msg = nullptr;
            if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
                std::cerr << RED << "❌ Error creating table: " << err_msg << RESET << std::endl;
                sqlite3_free(err_msg);
            }
        }
    }
    ~ErrorKnowledgeBase() { if(db) sqlite3_close(db); }

    void store(const std::string& pkg, const std::string& py_ver, const std::string& exc, const std::string& fix = "") {
        if (!db) return;
        std::lock_guard<std::mutex> lock(m_db);
        const char* sql = "INSERT INTO exceptions (package, python_version, exception_text, suggested_fix) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, pkg.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, py_ver.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, exc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, fix.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    std::string lookup_fix(const std::string& exc) {
        if (!db) return "";
        std::lock_guard<std::mutex> lock(m_db);
        const char* sql = "SELECT suggested_fix FROM exceptions WHERE (exception_text = ? OR ? LIKE '%' || exception_text || '%') AND suggested_fix != '' ORDER BY (length(exception_text)) DESC LIMIT 1;";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, exc.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, exc.c_str(), -1, SQLITE_TRANSIENT);
        std::string fix = "";
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* text = sqlite3_column_text(stmt, 0);
            if (text) fix = reinterpret_cast<const char*>(text);
        }
        sqlite3_finalize(stmt);
        return fix;
    }

    std::vector<std::pair<std::string, std::string>> get_fixes_for_pkg(const std::string& pkg) {
        std::vector<std::pair<std::string, std::string>> fixes;
        if (!db) return fixes;
        std::lock_guard<std::mutex> lock(m_db);
        const char* sql = "SELECT exception_text, suggested_fix FROM exceptions WHERE package = ? AND suggested_fix != '';";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, pkg.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            fixes.push_back({
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)),
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))
            });
        }
        sqlite3_finalize(stmt);
        return fixes;
    }
};

class TelemetryLogger {
    const Config& cfg;
    std::string test_id;
    std::atomic<bool> running{false};
    std::thread worker;
    sqlite3* db = nullptr;
    sqlite3_stmt* insert_stmt = nullptr;

    const size_t MAX_CORES = 1024;
    std::vector<uint64_t> last_user_vec;
    std::vector<uint64_t> last_sys_vec;
    std::vector<uint64_t> last_io_vec;
    uint64_t last_net_in = 0, last_net_out = 0;

public:
    TelemetryLogger(const Config& c, const std::string& id) 
        : cfg(c), test_id(id), 
          last_user_vec(MAX_CORES, 0), last_sys_vec(MAX_CORES, 0), last_io_vec(MAX_CORES, 0) 
    {
        fs::path db_path = cfg.spip_root / "telemetry.db";
        if (sqlite3_open(db_path.c_str(), &db) != SQLITE_OK) {
            std::cerr << "❌ Failed to open telemetry database." << std::endl;
            return;
        }

        sqlite3_busy_timeout(db, 10000); // 10s busy timeout for high-concurrency DB access

        const char* sql = "CREATE TABLE IF NOT EXISTS telemetry ("
                          "test_id TEXT, timestamp REAL, core_id INTEGER, cpu_user REAL, cpu_sys REAL, "
                          "mem_kb INTEGER, net_in INTEGER, net_out INTEGER, disk_read INTEGER, disk_write INTEGER, "
                          "iowait REAL);";
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);

        const char* insert_sql = "INSERT INTO telemetry VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr);
    }

    ~TelemetryLogger() {
        stop();
        if (insert_stmt) sqlite3_finalize(insert_stmt);
        if (db) sqlite3_close(db);
    }

    void start() {
        if (running) return;
        running = true;
        worker = std::thread(&TelemetryLogger::loop, this);
    }

    void stop() {
        if (!running) return;
        running = false;
        if (worker.joinable()) worker.join();
    }

private:
    void loop() {
        while (running) {
            auto start_time = std::chrono::steady_clock::now();
            sample();
            auto end_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
            if (elapsed < std::chrono::milliseconds(100)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100) - elapsed);
            }
        }
    }

    void sample() {
        double ts = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        
        sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

        #ifdef __APPLE__
        natural_t cpuCount;
        processor_info_array_t infoArray;
        mach_msg_type_number_t infoCount;
        kern_return_t kr = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount, &infoArray, &infoCount);
        
        if (kr == KERN_SUCCESS) {
            for (natural_t i = 0; i < cpuCount && i < MAX_CORES; ++i) {
                uint64_t u = infoArray[i * CPU_STATE_MAX + CPU_STATE_USER];
                uint64_t s = infoArray[i * CPU_STATE_MAX + CPU_STATE_SYSTEM];
                uint64_t id = infoArray[i * CPU_STATE_MAX + CPU_STATE_IDLE];
                
                double du = (u > last_user_vec[i]) ? (double)(u - last_user_vec[i]) : 0;
                double ds = (s > last_sys_vec[i]) ? (double)(s - last_sys_vec[i]) : 0;
                
                last_user_vec[i] = u; last_sys_vec[i] = s;
                
                log_to_db(ts, (int)i, du, ds, 0, 0, 0, 0, 0, 0); 
            }
            vm_deallocate(mach_task_self(), (vm_address_t)infoArray, infoCount * sizeof(int));
        }

        // Memory
        mach_msg_type_number_t count = HOST_VM_INFO_COUNT;
        vm_statistics_data_t vm_stats;
        if (host_statistics(mach_host_self(), HOST_VM_INFO, (host_info_t)&vm_stats, &count) == KERN_SUCCESS) {
            long used_mem = (vm_stats.active_count + vm_stats.wire_count) * (vm_page_size / 1024);
            log_to_db(ts, -1, 0, 0, used_mem, 0, 0, 0, 0, 0);
        }

        // Net
        struct ifaddrs *ifa_list = nullptr, *ifa;
        if (getifaddrs(&ifa_list) == 0) {
            uint64_t ibytes = 0, obytes = 0;
            for (ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr->sa_family == AF_LINK) {
                    struct if_data *ifd = (struct if_data *)ifa->ifa_data;
                    ibytes += ifd->ifi_ibytes;
                    obytes += ifd->ifi_obytes;
                }
            }
            log_to_db(ts, -2, 0, 0, 0, (long)(ibytes - last_net_in), (long)(obytes - last_net_out), 0, 0, 0);
            last_net_in = ibytes; last_net_out = obytes;
            freeifaddrs(ifa_list);
        }
        #elif defined(__linux__)
        // CPU (/proc/stat)
        std::ifstream stat("/proc/stat");
        std::string line;
        while (std::getline(stat, line) && line.starts_with("cpu")) {
            if (line.starts_with("cpu ")) continue; // Skip aggregate
            std::stringstream ss(line);
            std::string cpu_label; ss >> cpu_label;
            int core_id = std::stoi(cpu_label.substr(3));
            if (core_id >= (int)MAX_CORES) break;

            uint64_t u, n, s, id, io, irq, soft;
            if (!(ss >> u >> n >> s >> id >> io >> irq >> soft)) continue;
            
            uint64_t cur_user = u + n;
            uint64_t cur_sys = s + irq + soft;
            
            double du = (cur_user > last_user_vec[core_id]) ? (double)(cur_user - last_user_vec[core_id]) : 0;
            double ds = (cur_sys > last_sys_vec[core_id]) ? (double)(cur_sys - last_sys_vec[core_id]) : 0;
            double dio = (io > last_io_vec[core_id]) ? (double)(io - last_io_vec[core_id]) : 0;
            
            last_user_vec[core_id] = cur_user;
            last_sys_vec[core_id] = cur_sys;
            last_io_vec[core_id] = io;
            
            log_to_db(ts, core_id, du, ds, 0, 0, 0, 0, 0, dio);
        }

        // Memory (/proc/meminfo)
        std::ifstream meminfo("/proc/meminfo");
        long total = 0, free = 0, cached = 0, buffers = 0;
        while (std::getline(meminfo, line)) {
            if (line.starts_with("MemTotal:")) total = std::stol(line.substr(10));
            else if (line.starts_with("MemFree:")) free = std::stol(line.substr(10));
            else if (line.starts_with("Cached:")) cached = std::stol(line.substr(10));
            else if (line.starts_with("Buffers:")) buffers = std::stol(line.substr(10));
        }
        log_to_db(ts, -1, 0, 0, total - free - cached - buffers, 0, 0, 0, 0, 0);

        // Network (/proc/net/dev)
        std::ifstream netdev("/proc/net/dev");
        uint64_t rb = 0, tb = 0;
        std::getline(netdev, line); std::getline(netdev, line); // Skip headers
        while (std::getline(netdev, line)) {
            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::stringstream ss(line.substr(colon + 1));
            uint64_t r, dummy;
            ss >> r >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy >> dummy;
            rb += r; ss >> r; tb += r;
        }
        log_to_db(ts, -2, 0, 0, 0, (long)(rb - last_net_in), (long)(tb - last_net_out), 0, 0, 0);
        last_net_in = rb; last_net_out = tb;
        #endif

        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    }

    void log_to_db(double ts, int core, double u, double s, long mem, long ni, long no, long dr, long dw, double wait) {
        if (!insert_stmt) return;
        sqlite3_reset(insert_stmt);
        sqlite3_bind_text(insert_stmt, 1, test_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(insert_stmt, 2, ts);
        sqlite3_bind_int(insert_stmt, 3, core);
        sqlite3_bind_double(insert_stmt, 4, u);
        sqlite3_bind_double(insert_stmt, 5, s);
        sqlite3_bind_int64(insert_stmt, 6, mem);
        sqlite3_bind_int64(insert_stmt, 7, ni);
        sqlite3_bind_int64(insert_stmt, 8, no);
        sqlite3_bind_int64(insert_stmt, 9, dr);
        sqlite3_bind_int64(insert_stmt, 10, dw);
        sqlite3_bind_double(insert_stmt, 11, wait);
        sqlite3_step(insert_stmt);
    }
};

std::string compute_hash(const std::string& s) {
    // Robust, deterministic FNV-1a like mixing for project identifiers
    uint64_t h = 0xcbf29ce484222325;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001b3;
    }
    std::stringstream ss; ss << std::hex << h;
    return ss.str().substr(0, 16);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) parts.push_back(item);
    return parts;
}

std::string quote_arg(const std::string& arg) {
    // Full shell sanitization: escape all backslashes, double quotes, and dollars
    // Then wrap in double quotes. This prevents most injection vectors in POSIX shells.
    std::string result = "\"";
    for (char c : arg) {
        if (c == '\"' || c == '\\' || c == '$' || c == '`') result += "\\";
        result += c;
    }
    result += "\"";
    return result;
}

std::string get_exec_output(const std::string& cmd) {
    std::string result;
    char buffer[128];
    // Capture stderr by redirecting it to stdout
    std::string full_cmd = cmd + " 2>&1";
    FILE* pipe = popen(full_cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != NULL) {
        result += buffer;
    }
    pclose(pipe);
    if (!result.empty() && result.back() == '\n') result.pop_back();
    return result;
}

Config init_config() {
    const char* home = std::getenv("HOME");
    if (!home) {
        std::cerr << RED << "Error: HOME environment variable not set." << RESET << std::endl;
        std::exit(1);
    }
    
    Config cfg;
    cfg.home_dir = fs::path(home);
    cfg.spip_root = cfg.home_dir / ".spip";
    cfg.repo_path = cfg.spip_root / "repo";
    cfg.envs_root = cfg.spip_root / "envs";
    cfg.current_project = fs::current_path();
    cfg.project_hash = compute_hash(cfg.current_project.string());
    cfg.project_env_path = cfg.envs_root / cfg.project_hash;
    cfg.db_file = cfg.spip_root / "knowledge_base.db";

    return cfg;
}

void ensure_scripts(const Config& cfg) {
    fs::path scripts_dir = cfg.spip_root / "scripts";
    if (!fs::exists(scripts_dir)) fs::create_directories(scripts_dir);
    
    // In a real install, these would be bundled. For now, we assume they are in the project scripts/ dir.
    // If not found in project scripts/, we don't overwrite if they exist in spip_root/scripts.
    std::vector<std::string> script_names = {
        "safe_extract.py", "audit_helper.py", "review_helper.py", 
        "verify_helper.py", "trim_helper.py", "agent_helper.py",
        "pyc_profiler.py", "profile_ai_review.py"
    };
    
    fs::path project_scripts = fs::current_path() / "scripts";
    if (fs::exists(project_scripts)) {
        for (const auto& name : script_names) {
            fs::path src = project_scripts / name;
            fs::path dst = scripts_dir / name;
            if (fs::exists(src)) {
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            }
        }
    }
}

void ensure_dirs(const Config& cfg) {
    if (!fs::exists(cfg.spip_root)) fs::create_directories(cfg.spip_root);
    if (!fs::exists(cfg.envs_root)) fs::create_directories(cfg.envs_root);
    ensure_scripts(cfg);
    if (!fs::exists(cfg.repo_path)) {
        std::cout << "Creating repo at: " << cfg.repo_path << std::endl;
        fs::create_directories(cfg.repo_path);
        std::string cmd = std::format("cd {} && git init && git commit --allow-empty -m \"Initial commit\"", quote_arg(cfg.repo_path.string()));
        std::system(cmd.c_str());
        std::ofstream gitignore(cfg.repo_path / ".gitignore");
        gitignore << "# Full environment tracking\n";
        gitignore.close();
    }
}

bool branch_exists(const Config& cfg, const std::string& branch) {
    // get_exec_output appends " 2>&1" so we capture stderr too.
    // If branch doesn't exist, git prints "fatal: ..." which get_exec_output captures.
    // We must check if output is a valid hash vs error message.
    std::string cmd = std::format("cd {} && git rev-parse --verify {}", quote_arg(cfg.repo_path.string()), quote_arg(branch));
    std::string out = get_exec_output(cmd);
    if (out.empty()) return false;
    if (out.find("fatal") != std::string::npos) return false;
    if (out.find("error") != std::string::npos) return false;
    return true;
}



std::string get_platform_tuple() {
#ifdef __APPLE__
    #if defined(__aarch64__)
        return "aarch64-apple-darwin";
    #else
        return "x86_64-apple-darwin";
    #endif
#elif defined(__linux__)
    return "x86_64-unknown-linux-gnu";
#else
    return "unknown";
#endif
}

std::string get_full_version_map(const std::string& short_ver) {
    if (short_ver == "3.13") return "3.13.0";
    if (short_ver == "3.12") return "3.12.7";
    if (short_ver == "3.11") return "3.11.9";
    if (short_ver == "3.10") return "3.10.16"; // Was 3.10.19 in recent tags, safe fallback
    if (short_ver == "3.9") return "3.9.21";
    if (short_ver == "3.8") return "3.8.20";
    if (short_ver == "3.7") return "3.7.17"; // 20241016 tag supports 3.7.17
    if (short_ver == "2.7") return "2.7.18"; // 20241016 tag supports 2.7.18
    return short_ver + ".0"; // Fallback guess
}

std::string ensure_python_bin(const Config& cfg, const std::string& version) {
    std::string safe_v = "";
    for(char c : version) if(std::isalnum(c) || c == '.') safe_v += c;

    // 1. Check system path
    std::string python_bin = "python" + safe_v;
    std::string path_check = std::format("command -v {} 2>/dev/null", python_bin);
    if (!get_exec_output(path_check).empty()) {
        return python_bin; 
    }
    
    // For 2.7, simply 'python2' check
    if (safe_v == "2.7") {
         if (!get_exec_output("command -v python2 2>/dev/null").empty()) return "python2";
    }

    // 2. Check local standalones
    fs::path pythons_dir = cfg.spip_root / "pythons";
    fs::path local_python = pythons_dir / safe_v / "bin" / ("python" + safe_v); 
    // Usually standalone expands to python/bin/python3 or python/bin/python for 2.7
    
    fs::path install_bin_dir = pythons_dir / safe_v / "python" / "bin";
    
    if (safe_v == "2.7") {
        if (fs::exists(install_bin_dir / "python")) return (install_bin_dir / "python").string();
        if (fs::exists(install_bin_dir / "python2")) return (install_bin_dir / "python2").string();
    } else {
        if (fs::exists(install_bin_dir / "python3")) return (install_bin_dir / "python3").string();
    }
    // Check if previously assumed path exists
    if (fs::exists(local_python)) return local_python.string();

    // 3. Download if missing
    std::cout << YELLOW << "⚠️  " << python_bin << " not found. Downloading standalone build..." << RESET << std::endl;
    if (!fs::exists(pythons_dir)) fs::create_directories(pythons_dir);

    // Using 20241016 which is very stable for these versions
    std::string tag = "20241016"; 
    std::string full_ver = get_full_version_map(safe_v);
    std::string platform = get_platform_tuple();
    
    // Url construction: 
    // https://github.com/indygreg/python-build-standalone/releases/download/20241016/cpython-3.12.7+20241016-aarch64-apple-darwin-install_only.tar.gz
    std::string filename = std::format("cpython-{}+{}-{}-install_only.tar.gz", full_ver, tag, platform);
    std::string url = std::format("https://github.com/indygreg/python-build-standalone/releases/download/{}/{}", tag, filename);
    
    fs::path archive_path = pythons_dir / filename;
    fs::path dest_dir = pythons_dir / safe_v;
    
    std::cout << BLUE << "📥 Downloading " << url << "..." << RESET << std::endl;
    std::string dl_cmd = std::format("curl -L -s -# {} -o {}", quote_arg(url), quote_arg(archive_path.string()));
    int ret = std::system(dl_cmd.c_str());
    
    if (ret != 0 || !fs::exists(archive_path) || fs::file_size(archive_path) < 1000) {
        std::cerr << RED << "❌ Failed to download Python " << full_ver << " from " << url << RESET << std::endl;
        // Fallback to python3 if really desperate
        return "python3"; 
    }
    
    std::cout << BLUE << "📦 Unpacking to " << dest_dir.string() << "..." << RESET << std::endl;
    fs::create_directories(dest_dir);
    // tar -xzf archive -C dest
    std::string tar_cmd = std::format("tar -xzf {} -C {}", quote_arg(archive_path.string()), quote_arg(dest_dir.string()));
    std::system(tar_cmd.c_str());
    fs::remove(archive_path);
    
    // Determine path after unpack
    // Usually ./python/bin/python3
    local_python = dest_dir / "python" / "bin" / "python3";
    if (safe_v == "2.7") {
         local_python = dest_dir / "python" / "bin" / "python";
    }
    if (fs::exists(local_python)) return local_python.string();
    
    return "python3"; // Fallback
}

void create_base_version(const Config& cfg, const std::string& version) {

    std::string branch = "base/" + version;
    if (branch_exists(cfg, branch)) return;

    {
        // Use lock for logging consistency if called from parallel context
        // But here we rely on stdout mixing being acceptable or protected by caller (it is protected by caller).
        std::cout << MAGENTA << "🔨 Bootstrapping base Python " << version << "..." << RESET << std::endl;
    }
    
    // Sanitize version
    std::string safe_v = "";
    for(char c : version) if(std::isalnum(c) || c == '.') safe_v += c;

    fs::path temp_venv = cfg.spip_root / ("temp_venv_" + safe_v);
    
    // Try pythonX.Y, fallback to python3 if matching version
    std::string python_bin = ensure_python_bin(cfg, safe_v);

    std::string venv_cmd = std::format("{} -m venv {}", quote_arg(python_bin), quote_arg(temp_venv.string()));
    int ret = std::system(venv_cmd.c_str());
    if (ret != 0) {
        // Only fallback if ensure_python returned something broken, which implies download failed or system broken
        std::cerr << RED << "❌ Failed to create venv with " << python_bin << RESET << std::endl;
        std::exit(1);
    }

    if (ret != 0) {
        std::cerr << RED << "❌ Failed to create venv for python " << version << RESET << std::endl;
        std::exit(1);
    }

    // Git command: Handle master vs main
    std::string git_cmd = std::format(
        "cd \"{}\" && (git checkout master || git checkout main) && git checkout -b \"{}\" && "
        "rm -rf * && cp -r \"{}/\"* . && git add -A && git commit -m \"Base Python {}\" && "
        "(git checkout master || git checkout main)",
        cfg.repo_path.string(), branch, temp_venv.string(), version
    );
    
    if (std::system(git_cmd.c_str()) != 0) {
        std::cerr << RED << "❌ Failed to commit base version " << version << RESET << std::endl;
        // Don't exit, maybe retry? But likely fatal.
        // Clean up
        fs::remove_all(temp_venv);
        std::exit(1);
    }
    fs::remove_all(temp_venv);
}

void setup_project_env(const Config& cfg, const std::string& version = "3") {
    ensure_dirs(cfg);
    std::string branch = "project/" + cfg.project_hash;

    if (!branch_exists(cfg, branch)) {
        std::string base_branch = "base/" + version;
        if (!branch_exists(cfg, base_branch)) {
            create_base_version(cfg, version);
        }
        std::cout << MAGENTA << "🔨 Bootstrapping base Python " << version << "..." << RESET << std::endl;
        create_base_version(cfg, version);
        std::cout << GREEN << "🌟 Creating new environment branch: " << branch << RESET << std::endl;
        std::string cmd = std::format("cd {} && git branch {} {}", 
            quote_arg(cfg.repo_path.string()), quote_arg(branch), quote_arg(base_branch));
        std::system(cmd.c_str());
    }

    if (!fs::exists(cfg.project_env_path)) {
        std::cout << CYAN << "📂 Linking worktree for project..." << RESET << std::endl;
        std::system(std::format("cd {} && git checkout main 2>/dev/null", quote_arg(cfg.repo_path.string())).c_str());
        std::string cmd = std::format("cd {} && git worktree add {} {}", 
            quote_arg(cfg.repo_path.string()), quote_arg(cfg.project_env_path.string()), quote_arg(branch));
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::system(std::format("cd {} && git worktree prune", quote_arg(cfg.repo_path.string())).c_str());
            std::system(cmd.c_str());
        }
        std::ofstream os(cfg.project_env_path / ".project_origin");
        os << cfg.current_project.string();
    }
}

void commit_state(const Config& cfg, const std::string& msg) {
    std::string cmd = std::format("cd {} && git add -A && git commit -m {} --allow-empty", 
        quote_arg(cfg.project_env_path.string()), quote_arg(msg));
    std::system(cmd.c_str());
}

void init_db() {
    const char* home = std::getenv("HOME");
    fs::path db_path = fs::path(home) / ".spip" / "db";
    if (!fs::exists(db_path)) {
        fs::create_directories(db_path);
        std::string cmd = std::format("cd \"{}\" && git init && git commit --allow-empty -m \"Initial DB commit\"", db_path.string());
        std::system(cmd.c_str());
    }
}

fs::path get_db_path(const std::string& pkg) {
    const char* home = std::getenv("HOME");
    fs::path db_root = fs::path(home) / ".spip" / "db";
    std::string name = pkg;
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    std::string p1 = (name.length() > 0) ? name.substr(0, 1) : "_";
    std::string p2 = (name.length() > 1) ? name.substr(0, 2) : p1 + "_";
    return db_root / "packages" / p1 / p2 / (name + ".json");
}

fs::path get_site_packages(const Config& cfg) {
    if (!fs::exists(cfg.project_env_path)) return fs::path();
    // Safety: prevent throwing if directory doesn't exist
    try {
        for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
            if (entry.is_directory() && entry.path().filename() == "site-packages") {
                return entry.path();
            }
        }
    } catch (...) {}
    return fs::path();
}

// Helper: Execute command with setup
void exec_with_setup(Config& cfg, std::function<void(Config&)> func) {
    setup_project_env(cfg);
    func(cfg);
}

// Helper: Validate argument count
bool require_args(const std::vector<std::string>& args, size_t min_count, const std::string& usage_msg) {
    if (args.size() < min_count) {
        std::cout << usage_msg << std::endl;
        return false;
    }
    return true;
}


void benchmark_mirrors(Config& cfg) {
    std::cout << MAGENTA << "🏎  Benchmarking mirrors to find the fastest..." << RESET << std::endl;
    std::vector<std::pair<std::string, std::string>> mirrors = {
        {"PyPI Official", "https://pypi.org"},
        {"Tsinghua", "https://pypi.tuna.tsinghua.edu.cn"},
        {"USTC", "https://pypi.mirrors.ustc.edu.cn"},
        {"Baidu", "https://mirror.baidu.com/pypi"},
        {"Aliyun", "https://mirrors.aliyun.com/pypi"}
    };
    
    std::string best_mirror = "https://pypi.org";
    double min_time = 9999.0;

    for (const auto& [name, url] : mirrors) {
        // Strict 4s timeout as per user rule 0
        std::string cmd = std::format("timeout -s 9 4s curl -o /dev/null -s -w \"%{{time_total}}\" -m 3 \"{}/pypi/pip/json\"", url);
        std::string out = get_exec_output(cmd);
        try {
            double t = std::stod(out);
            if (t > 0 && t < min_time) {
                min_time = t;
                best_mirror = url;
            }
            std::cout << "  - [" << name << "] " << url << ": " << GREEN << t << "s" << RESET << std::endl;
        } catch (...) {
            std::cout << "  - [" << name << "] " << url << ": " << RED << "Timeout/Error" << RESET << std::endl;
        }
    }
    cfg.pypi_mirror = best_mirror;
    std::cout << GREEN << "✨ Selected " << best_mirror << " (Time: " << min_time << "s)" << RESET << std::endl;
}

void fetch_package_metadata(const Config& cfg, const std::string& pkg) {
    fs::path target = get_db_path(pkg);
    if (fs::exists(target) && fs::file_size(target) > 0) return;
    
    static std::mutex m_fetch;
    std::lock_guard<std::mutex> lock(m_fetch);
    
    if (fs::exists(target) && fs::file_size(target) > 0) return;

    fs::create_directories(target.parent_path());
    std::string url = std::format("{}/pypi/{}/json", cfg.pypi_mirror, pkg);
    // Use temporary file for atomic write
    fs::path temp_target = target;
    temp_target += ".tmp";
    std::string cmd = std::format("curl -s -L \"{}\" -o \"{}\"", url, temp_target.string());
    std::system(cmd.c_str());
    if (fs::exists(temp_target)) {
        fs::rename(temp_target, target);
    }
}

void db_worker(std::queue<std::string>& q, std::mutex& m, std::atomic<int>& count, int total, Config cfg) {
    while (true) {
        std::string pkg;
        {
            std::lock_guard<std::mutex> lock(m);
            if (q.empty()) return;
            pkg = q.front();
            q.pop();
        }
        fetch_package_metadata(cfg, pkg); // Pass by value/copy is fine for thread
        int c = ++count;
        if (c % 100 == 0) {
            std::cout << "\rFetched " << c << "/" << total << std::flush;
        }
    }
}

struct PackageInfo {
    std::string name;
    std::string version;
    std::string wheel_url;
    std::string requires_python;
    std::vector<std::string> dependencies;
};

std::string extract_field(const std::string& json, const std::string& key) {
    // Hardened regex: strictly match key and string value, limited backtracking
    std::string pattern = "\"" + key + "\":\\s*\"([^\"]*?)\"";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re)) return match[1];
    return "";
}

std::vector<std::string> extract_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    // Hardened regex: find array start, capture content until first closing bracket
    std::string pattern = "\"" + key + "\":\\s*\\[(.*?)\\]";
    std::regex re(pattern);
    std::smatch match;
    if (std::regex_search(json, match, re)) {
        std::string array_content = match[1];
        std::regex item_re("\"([^\"]*?)\"");
        auto words_begin = std::sregex_iterator(array_content.begin(), array_content.end(), item_re);
        auto words_end = std::sregex_iterator();
        for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
            result.push_back((*i)[1]);
        }
    }
    return result;
}

std::vector<std::string> get_all_versions(const std::string& pkg) {
    fs::path db_file = get_db_path(pkg);
    if (!fs::exists(db_file)) {
        Config cfg = init_config(); 
        fetch_package_metadata(cfg, pkg);
    }
    
    std::vector<std::string> versions;
    
    // Improved Regex approach for finding versions keys
    if (fs::exists(db_file)) {
        std::ifstream ifs(db_file);
        std::string json((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>()); 
        
        // Find "releases": {
        size_t rel_pos = json.find("\"releases\"");
        if (rel_pos == std::string::npos) return {};
        
        std::regex ver_re(R"(\"([0-9]+\.[0-9]+(\.[0-9]+)?([a-zA-Z0-9]+)?)\"\s*:)");
        
        auto begin = std::sregex_iterator(json.begin() + rel_pos, json.end(), ver_re);
        auto end = std::sregex_iterator();
        
        for (std::sregex_iterator i = begin; i != end; ++i) {
            std::smatch match = *i;
            versions.push_back(match[1].str());
        }
    }
    
    // Semantic sort
    std::stable_sort(versions.begin(), versions.end(), [](const std::string& a, const std::string& b) {
        // Very basic semantic version comparison
        auto parse_ver = [](std::string_view s) {
            std::vector<int> parts;
            std::string part;
            for (char c : s) {
                if (isdigit(c)) part += c;
                else { if(!part.empty()) parts.push_back(std::stoi(part)); part=""; }
            }
            if (!part.empty()) parts.push_back(std::stoi(part));
            return parts;
        };
        return parse_ver(a) < parse_ver(b);
    });

    return versions;
}

int score_wheel(const std::string& url, const std::string& target_py = "3.12") {
    int score = 0;
    std::string lower = url;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    
    // Platform matching (Mac ARM64 priority)
    if (lower.find("macosx") != std::string::npos) {
        if (lower.find("arm64") != std::string::npos) score += 1000;
        else if (lower.find("universal2") != std::string::npos) score += 500;
        else if (lower.find("x86_64") != std::string::npos) score += 100;
    } else if (lower.find("none-any.whl") != std::string::npos) {
        score += 50;
    } else {
        return -1; // Incompatible platform (linux/win)
    }

    // Python version matching
    std::string py_tag = "cp" + target_py;
    py_tag.erase(std::remove(py_tag.begin(), py_tag.end(), '.'), py_tag.end());
    if (lower.find(py_tag) != std::string::npos) score += 200;
    else if (lower.find("py3-none-any") != std::string::npos) score += 100;
    else if (lower.find("py2.py3-none-any") != std::string::npos) score += 100;

    return score;
}

fs::path get_cached_wheel_path(const Config& cfg, const PackageInfo& info) {
    // Should match parallel_download logic: just flattened name
    std::string filename = info.name + "-" + info.version + ".whl";
    return cfg.spip_root / filename; 
}

PackageInfo get_package_info(const std::string& pkg, const std::string& version = "", const std::string& target_py = "3.12") {
    fs::path db_file = get_db_path(pkg);
    if (!fs::exists(db_file)) {
        std::cout << YELLOW << "⚠️ Metadata for " << pkg << " not in local DB. Fetching..." << RESET << std::endl;
        Config cfg = init_config(); 
        fetch_package_metadata(cfg, pkg);
    }
    std::ifstream ifs(db_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    PackageInfo info;
    if (content.empty()) return info;
    info.name = pkg;
    
    if (version.empty()) {
        // Extract version from "info" section (latest)
        size_t info_pos = content.find("\"info\"");
        if (info_pos != std::string::npos) {
            size_t ver_key_pos = content.find("\"version\"", info_pos);
            if (ver_key_pos != std::string::npos) {
                size_t colon_pos = content.find(":", ver_key_pos);
                if (colon_pos != std::string::npos) {
                    size_t val_start = content.find("\"", colon_pos);
                    if (val_start != std::string::npos) {
                        size_t val_end = content.find("\"", val_start + 1);
                        if (val_end != std::string::npos) {
                            info.version = content.substr(val_start + 1, val_end - val_start - 1);
                        }
                    }
                }
            }
        }
        if (info.version.empty()) info.version = extract_field(content, "version");
    } else {
        info.version = version;
    }
    
    info.requires_python = extract_field(content, "requires_python");
    // Some versions might have it in the specific release info instead of top-level info if not latest
    if (info.requires_python.empty()) {
        size_t rel_pos = content.find("\"releases\"");
        if (rel_pos != std::string::npos) {
            std::string ver_key = "\"" + info.version + "\"";
            size_t ver_entry = content.find(ver_key, rel_pos);
            if (ver_entry != std::string::npos) {
                info.requires_python = extract_field(content.substr(ver_entry), "requires_python");
            }
        }
    }

    auto raw_deps = extract_array(content, "requires_dist");
    for (const auto& d : raw_deps) {
        std::regex dep_re(R"(^([a-zA-Z0-9_.-]+)([^;]*)(;.*)?)");
        std::smatch m;
        if (std::regex_search(d, m, dep_re)) {
            std::string name = m[1].str();
            std::string extra_part = m[3].matched ? m[3].str() : "";
            if (extra_part.find("extra ==") == std::string::npos) {
                info.dependencies.push_back(name);
            }
        }
    }

    // Robust wheel URL extraction for the SPECIFIC version
    size_t rel_pos = content.find("\"releases\"");
    if (rel_pos != std::string::npos) {
        std::string ver_key = "\"" + info.version + "\"";
        size_t ver_entry = content.find(ver_key, rel_pos);
        if (ver_entry != std::string::npos) {
            size_t open_bracket = content.find("[", ver_entry);
            size_t close_bracket = std::string::npos;
            // Search for next version key to ensure we don't overrun (approximate)
            // or just find matching bracket. Finding matching bracket in C++ without counter is basic but risky if nested (unlikely for release list).
            // Using a simple counter for bracket matching would be safer 
            // but for now, assuming list of objects structure.
            
            // Refined: find closing bracket carefully.
            int balance = 1;
            size_t cur = open_bracket + 1;
            while (cur < content.size() && balance > 0) {
                if (content[cur] == '[') balance++;
                else if (content[cur] == ']') balance--;
                cur++;
            }
            close_bracket = cur - 1;

            if (open_bracket != std::string::npos && balance == 0) {
                std::string release_data = content.substr(open_bracket, close_bracket - open_bracket + 1);
                
                std::regex url_re("\"url\":\\s*\"(https://[^\"]*\\.whl)\"");
                auto wheels_begin = std::sregex_iterator(release_data.begin(), release_data.end(), url_re);
                auto wheels_end = std::sregex_iterator();
                
                int best_score = -1;
                for (std::sregex_iterator it = wheels_begin; it != wheels_end; ++it) {
                    std::string url = (*it)[1].str();
                    int s = score_wheel(url, target_py);
                    if (s > best_score) {
                        best_score = s;
                        info.wheel_url = url;
                    }
                }
            }
        }
    }
    // Fallback: if specific version wheel not found, do NOT fallback to random wheel.
    // This ensures we don't install mismatched versions.

    return info;
}

void resolve_and_install(const Config& cfg, const std::vector<std::string>& targets, const std::string& version = "", const std::string& target_py = "3.12") {
    std::vector<std::string> queue = targets;
    std::set<std::string> installed;
    std::map<std::string, PackageInfo> resolved;
    std::cout << MAGENTA << "🔍 Resolving dependencies..." << RESET << std::endl;
    size_t i = 0;
    while(i < queue.size()) {
        std::string name = queue[i++];
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        std::replace(lower_name.begin(), lower_name.end(), '_', '-');
        std::replace(lower_name.begin(), lower_name.end(), '.', '-');
        
        if (installed.count(lower_name)) continue;
        
        // Try to pass target_py if we can infer it (harder here, but default 3.12)
        PackageInfo info = (i == 1 && !version.empty()) ? get_package_info(name, version, target_py) : get_package_info(name, "", target_py);
        
        if (info.wheel_url.empty()) {
             std::cout << RED << "❌ Could not find wheel URL for " << name << RESET << std::endl;
             continue;
        }
        resolved[lower_name] = info;
        installed.insert(lower_name);
        for (const auto& d : info.dependencies) queue.push_back(d);
    }
    fs::path site_packages = get_site_packages(cfg);
    if (site_packages.empty()) {
        std::cerr << RED << "❌ Could not find site-packages directory." << RESET << std::endl;
        return;
    }
    std::cout << GREEN << "🚀 Installing " << resolved.size() << " packages..." << RESET << std::endl;
    int current = 0;
    for (const auto& [name, info] : resolved) {
        // Check if already installed
        // Simple heuristic: check for .dist-info directory
        // Note: Package names in dist-info are usually lowercased or underscored.
        std::string safe_name = name;
        std::replace(safe_name.begin(), safe_name.end(), '-', '_');
        fs::path dist_info = site_packages / (safe_name + "-" + info.version + ".dist-info");
        
        // Also check for just the package directory as a fallback/fast-check
        fs::path pkg_dir = site_packages / name;
        if (fs::exists(dist_info)) {
             std::cout << GREEN << "✔ " << name << " " << info.version << " already installed." << RESET << std::endl;
             continue;
        }

        current++;
        std::cout << BLUE << "[" << current << "/" << resolved.size() << "] " << RESET 
                  << "📦 " << BOLD << name << RESET << " (" << info.version << ")..." << std::endl;
        
        fs::path temp_wheel = cfg.spip_root / (name + "-" + info.version + ".whl");
        if (!fs::exists(temp_wheel) || fs::file_size(temp_wheel) == 0) {
            // Per-wheel lock to allow parallel downloads of different packages
            static std::mutex m_registry;
            static std::map<std::string, std::shared_ptr<std::mutex>> locks;
            
            std::shared_ptr<std::mutex> wheel_lock;
            {
                std::lock_guard<std::mutex> lock(m_registry);
                if (locks.find(info.wheel_url) == locks.end()) {
                    locks[info.wheel_url] = std::make_shared<std::mutex>();
                }
                wheel_lock = locks[info.wheel_url];
            }

            std::lock_guard<std::mutex> lock(*wheel_lock);
            if (!fs::exists(temp_wheel) || fs::file_size(temp_wheel) == 0) {
                // Use temporary file for atomic write
                fs::path partial = temp_wheel;
                partial += ".part";
                // Only show progress bars if not in high-concurrency mode to avoid garbled output
                bool quiet = (std::thread::hardware_concurrency() > 8);
                // Respecting user rule for external command timeouts, but using a larger window for large wheels
                std::string dl_cmd = std::format("timeout 300 curl -L --connect-timeout 10 --max-time 240 -s {} {} -o {}", 
                    quiet ? "" : "-#", quote_arg(info.wheel_url), quote_arg(partial.string()));
                std::system(dl_cmd.c_str());
                if (fs::exists(partial)) {
                    fs::rename(partial, temp_wheel);
                }
            }
        }
        
        // Critical: S-02 Safe Extraction to prevent path traversal
        fs::path extraction_helper = cfg.spip_root / "scripts" / "safe_extract.py";
        
        fs::path python_bin = cfg.project_env_path / "bin" / "python";
        std::string extract_cmd = std::format("{} {} {} {}", 
            quote_arg(python_bin.string()), quote_arg(extraction_helper.string()), 
            quote_arg(temp_wheel.string()), quote_arg(site_packages.string()));
        int ret = std::system(extract_cmd.c_str());
        
        if (ret != 0) {
            std::cerr << YELLOW << "⚠️ Extraction failed. Wheel might be corrupt. Deleting and retrying..." << RESET << std::endl;
            fs::remove(temp_wheel);
            
            // Retry download once
             std::string dl_cmd = std::format("curl -L -s -# {} -o {}", quote_arg(info.wheel_url), quote_arg(temp_wheel.string()));
             int dl_ret = std::system(dl_cmd.c_str());
             
             if (dl_ret == 0 && fs::exists(temp_wheel)) {
                 ret = std::system(extract_cmd.c_str());
             }
             
             if (ret != 0) {
                 std::cerr << RED << "❌ Installation failed for " << name << ". (Bad wheel or extraction error)" << RESET << std::endl;
                 // Clean up bad wheel to prevent future loops, unless debugging
                 if (fs::exists(temp_wheel)) fs::remove(temp_wheel); 
             }
        }
    }
}

// Forward declarations
void uninstall_package(const Config& cfg, const std::string& pkg);

void record_manual_install(const Config& cfg, const std::string& pkg, bool add) {
    fs::path manual_file = cfg.project_env_path / ".spip_manual";
    std::set<std::string> manual_pkgs;
    if (fs::exists(manual_file)) {
        std::ifstream ifs(manual_file);
        std::string line;
        while (std::getline(ifs, line)) if (!line.empty()) manual_pkgs.insert(line);
    }
    std::string lower_pkg = pkg;
    std::transform(lower_pkg.begin(), lower_pkg.end(), lower_pkg.begin(), ::tolower);
    if (add) manual_pkgs.insert(lower_pkg);
    else manual_pkgs.erase(lower_pkg);

    std::ofstream ofs(manual_file);
    for (const auto& p : manual_pkgs) ofs << p << "\n";
}

void prune_orphans(const Config& cfg) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) return;

    fs::path manual_file = cfg.project_env_path / ".spip_manual";
    std::set<std::string> manual_pkgs;
    if (fs::exists(manual_file)) {
        std::ifstream ifs(manual_file);
        std::string line;
        while (std::getline(ifs, line)) if (!line.empty()) manual_pkgs.insert(line);
    }

    std::cout << MAGENTA << "🧹 Identifying orphaned packages..." << RESET << std::endl;
    
    // Build required set
    std::set<std::string> required;
    std::vector<std::string> queue(manual_pkgs.begin(), manual_pkgs.end());
    size_t i = 0;
    while(i < queue.size()) {
        std::string name = queue[i++];
        if (required.count(name)) continue;
        required.insert(name);
        PackageInfo info = get_package_info(name);
        for (const auto& d : info.dependencies) {
            std::string lower_d = d;
            std::transform(lower_d.begin(), lower_d.end(), lower_d.begin(), ::tolower);
            queue.push_back(lower_d);
        }
    }

    // Identify installed
    std::set<std::string> installed;
    for (const auto& entry : fs::directory_iterator(site_packages)) {
        if (entry.is_directory()) {
            std::string name = entry.path().filename().string();
            if (name.ends_with(".dist-info")) {
                std::string pkg = name.substr(0, name.find('-'));
                std::transform(pkg.begin(), pkg.end(), pkg.begin(), ::tolower);
                installed.insert(pkg);
            }
        }
    }

    std::vector<std::string> to_prune;
    for (const auto& pkg : installed) {
        if (!required.count(pkg)) to_prune.push_back(pkg);
    }

    if (to_prune.empty()) {
        std::cout << GREEN << "✨ No orphans found. Environment is clean." << RESET << std::endl;
        return;
    }

    std::cout << YELLOW << "Found " << to_prune.size() << " orphans: ";
    for (const auto& p : to_prune) std::cout << p << " ";
    std::cout << RESET << "\nPruning..." << std::endl;

    for (const auto& p : to_prune) {
        uninstall_package(cfg, p);
    }
    commit_state(cfg, "Pruned orphans");
    std::cout << GREEN << "✔️  Orphan pruning complete." << RESET << std::endl;
}



void uninstall_package(const Config& cfg, const std::string& pkg) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) return;
    std::string lower_pkg = pkg;
    std::transform(lower_pkg.begin(), lower_pkg.end(), lower_pkg.begin(), ::tolower);
    std::string norm_pkg = lower_pkg;
    std::replace(norm_pkg.begin(), norm_pkg.end(), '-', '_');
    fs::path dist_info;
    for (const auto& entry : fs::directory_iterator(site_packages)) {
        std::string lower_name = entry.path().filename().string();
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.find(norm_pkg) == 0 && lower_name.ends_with(".dist-info")) {
            dist_info = entry.path();
            break;
        }
    }
    if (dist_info.empty()) {
        std::cout << RED << "❌ Could not find installation metadata for " << pkg << RESET << std::endl;
        return;
    }
    std::cout << MAGENTA << "🗑 Uninstalling " << BOLD << pkg << RESET << "..." << std::endl;
    fs::path record_file = dist_info / "RECORD";
    if (fs::exists(record_file)) {
        std::ifstream ifs(record_file);
        std::string line;
        while (std::getline(ifs, line)) {
            size_t comma = line.find(',');
            if (comma != std::string::npos) {
                std::string rel_path = line.substr(0, comma);
                fs::path full_path = site_packages / rel_path;
                if (fs::exists(full_path) && !fs::is_directory(full_path)) {
                    fs::remove(full_path);
                    // Prune empty parent directories up to site_packages
                    fs::path p = full_path.parent_path();
                    while (p != site_packages && fs::exists(p) && fs::is_empty(p)) {
                        fs::remove(p);
                        p = p.parent_path();
                    }
                }
            }
        }
    }
    fs::remove_all(dist_info);
}

void print_tree(const std::string& pkg, int depth, std::set<std::string>& visited) {
    std::string indent = "";
    for (int i = 0; i < depth; ++i) indent += "  ";
    
    std::string lower_pkg = pkg;
    std::transform(lower_pkg.begin(), lower_pkg.end(), lower_pkg.begin(), ::tolower);

    if (visited.count(lower_pkg)) {
        std::cout << indent << "└── " << YELLOW << pkg << " (circular)" << RESET << std::endl;
        return;
    }
    visited.insert(lower_pkg);

    PackageInfo info = get_package_info(pkg);
    std::cout << indent << (depth == 0 ? "" : "└── ") << GREEN << pkg << RESET << " (" << info.version << ")" << std::endl;
    
    for (const auto& dep : info.dependencies) {
        print_tree(dep, depth + 1, visited);
    }
}

void run_package_tests(const Config& cfg, const std::string& pkg) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) {
        std::cerr << RED << "❌ site-packages not found." << RESET << std::endl;
        return;
    }

    std::string lower_pkg = pkg;
    std::transform(lower_pkg.begin(), lower_pkg.end(), lower_pkg.begin(), ::tolower);
    fs::path pkg_path;
    for (const auto& entry : fs::directory_iterator(site_packages)) {
        std::string name = entry.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        if (name == lower_pkg) {
            pkg_path = entry.path();
            break;
        }
    }

    if (pkg_path.empty()) {
        std::cout << YELLOW << "⚠️ Could not find source directory for " << pkg << ". Looking for .dist-info..." << RESET << std::endl;
        // Search for the package name as a prefix
        for (const auto& entry : fs::directory_iterator(site_packages)) {
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), ::tolower);
            if (name.find(lower_pkg) == 0 && !name.ends_with(".dist-info")) {
                pkg_path = entry.path();
                break;
            }
        }
    }

    if (pkg_path.empty()) {
        std::cerr << RED << "❌ Could not locate package " << pkg << " in environment." << RESET << std::endl;
        return;
    }

    std::cout << MAGENTA << "🧪 Preparing to test " << pkg << " at " << pkg_path.string() << "..." << RESET << std::endl;
    
    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    // Check if pytest is installed using a quick python check
    std::string pytest_check = std::format("{} -c \"import importlib.util; exit(0 if importlib.util.find_spec('pytest') else 1)\"", quote_arg(python_bin.string()));
    if (std::system(pytest_check.c_str()) != 0) {
        std::cout << BLUE << "📦 Installing pytest for testing..." << RESET << std::endl;
        std::string install_pytest = std::format("{} -m pip install pytest", quote_arg(python_bin.string()));
        std::system(install_pytest.c_str());
        std::cout << GREEN << "✔️  pytest installed." << RESET << std::endl;
    }

    std::cout << GREEN << "🚀 Running tests..." << RESET << std::endl;
    std::string test_cmd = std::format("{} -m pytest {}", quote_arg(python_bin.string()), quote_arg(pkg_path.string()));
    std::system(test_cmd.c_str());
}

void run_all_package_tests(const Config& cfg) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) return;

    std::cout << MAGENTA << "🧪 Running tests for ALL installed packages..." << RESET << std::endl;
    
    std::set<std::string> pkgs;
    for (const auto& entry : fs::directory_iterator(site_packages)) {
        if (entry.is_directory()) {
            std::string name = entry.path().filename().string();
            if (name != "__pycache__" && !name.ends_with(".dist-info") && !name.ends_with(".egg-info") && name != "bin") {
                pkgs.insert(name);
            }
        }
    }

    for (const auto& pkg : pkgs) {
        run_package_tests(cfg, pkg);
    }
}

void boot_environment(const Config& cfg, const std::string& script_path) {
    fs::path boot_dir = cfg.spip_root / "boot";
    fs::path kernel = boot_dir / "vmlinuz";
    fs::path initrd = boot_dir / "initrd.img";

    if (!fs::exists(kernel) || !fs::exists(initrd)) {
        std::cout << YELLOW << "⚠️ Minimal Linux kernel or initrd not found in " << boot_dir << RESET << std::endl;
        std::cout << "Please place 'vmlinuz' and 'initrd.img' there to use virtualized execution." << std::endl;
        std::cout << "Suggested minimal kernel: https://github.com/amluto/virtme (or use a buildroot image)." << std::endl;
        return;
    }

    std::cout << MAGENTA << "🚀 Booting virtualized environment for " << script_path << "..." << RESET << std::endl;

    // Constructed QEMU command
    // -virtfs for 9p sharing of the environment
    // -append for kernel parameters (mount 9p, run script)
    
    std::string accel = "";
#ifdef __APPLE__
    accel = "-accel hvf -cpu host";
#else
    accel = "-accel kvm -cpu host";
#endif

    // We share the project environment path and the current project path (for the script)
    std::string qemu_cmd = std::format(
        "qemu-system-x86_64 {} -m 1G -nographic "
        "-kernel {} -initrd {} "
        "-virtfs local,path={},mount_tag=spip_env,security_model=none,id=spip_env "
        "-virtfs local,path={},mount_tag=project_root,security_model=none,id=project_root "
        "-append \"console=ttyS0 root=/dev/ram0 rw init=/sbin/init spip_script={}\" ",
        accel, quote_arg(kernel.string()), quote_arg(initrd.string()),
        quote_arg(cfg.project_env_path.string()), 
        quote_arg(cfg.current_project.string()),
        quote_arg(script_path)
    );

    std::cout << CYAN << "QEMU Command: " << qemu_cmd << RESET << std::endl;
    std::system(qemu_cmd.c_str());
}

void freeze_environment(const Config& cfg, const std::string& output_file) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) {
        std::cerr << RED << "❌ site-packages not found." << RESET << std::endl;
        return;
    }

    std::cout << MAGENTA << "🧊 Freezing environment to " << output_file << "..." << RESET << std::endl;
    // Archive site-packages and pyvenv.cfg for a portable freeze
    std::string tar_cmd = std::format("tar -czf \"{}\" -C \"{}\" . -C \"{}\" pyvenv.cfg", 
        output_file, site_packages.string(), cfg.project_env_path.string());
    
    int ret = std::system(tar_cmd.c_str());
    if (ret == 0) {
        std::cout << GREEN << "✨ Environment frozen successfully!" << RESET << std::endl;
    } else {
        std::cerr << RED << "❌ Failed to create archive." << RESET << std::endl;
    }
}

void audit_environment(const Config& cfg) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) return;

    std::cout << MAGENTA << "🛡 Performing security audit (OSV API)..." << RESET << std::endl;

    fs::path helper_path = cfg.spip_root / "scripts" / "audit_helper.py";

    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    std::string audit_cmd = std::format("\"{}\" \"{}\" \"{}\"", 
        python_bin.string(), helper_path.string(), site_packages.string());
    
    std::system(audit_cmd.c_str());
}

void review_code(const Config& cfg) {
    const char* api_key = std::getenv("GEMINI_API_KEY");
    if (!api_key) {
        std::cout << YELLOW << "⚠️ GEMINI_API_KEY not found in environment." << RESET << std::endl;
        std::cout << "To use AI review, set your key: export GEMINI_API_KEY='your-key'" << std::endl;
        return;
    }

    std::cout << MAGENTA << "🤖 Preparing AI Code Review (Gemini Pro)..." << RESET << std::endl;

    fs::path helper_path = cfg.spip_root / "scripts" / "review_helper.py";

    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    std::string review_cmd = std::format("\"{}\" \"{}\" \"{}\" \"{}\"", 
        python_bin.string(), helper_path.string(), api_key, cfg.current_project.string());
    
    std::system(review_cmd.c_str());
}

void verify_environment(const Config& cfg) {
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
    if (site_packages.empty()) return;

    std::cout << MAGENTA << "🔍 Verifying environment integrity (Syntax + Types)..." << RESET << std::endl;

    fs::path helper_path = cfg.spip_root / "scripts" / "verify_helper.py";

    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    std::string verify_cmd = std::format("{} {} {} {}", 
        quote_arg(python_bin.string()), quote_arg(helper_path.string()), 
        quote_arg(site_packages.string()), quote_arg((cfg.project_env_path / "bin").string()));
    
    int ret = std::system(verify_cmd.c_str());
    if (ret != 0) {
        std::cout << RED << "❌ VERIFICATION FAILED: Syntax errors detected in installed packages!" << RESET << std::endl;
        std::cout << YELLOW << "⚠️ Reverting environment state..." << RESET << std::endl;
        std::system(std::format("cd \"{}\" && git reset --hard HEAD^", cfg.project_env_path.string()).c_str());
        std::exit(1);
    } else {
        std::cout << GREEN << "✨ Verification complete. No syntax errors found." << RESET << std::endl;
    }
}

void trim_environment(const Config& cfg, const std::string& script_path) {
    if (!fs::exists(script_path)) {
        std::cerr << RED << "❌ Script not found: " << script_path << RESET << std::endl;
        return;
    }

    std::cout << MAGENTA << "✂️ Trimming environment based on " << script_path << "..." << RESET << std::endl;

    // Create a new branch for the trim
    std::string timestamp = std::format("{:x}", std::chrono::system_clock::now().time_since_epoch().count());
    std::string trim_branch = "trim/" + cfg.project_hash + "/" + timestamp.substr(timestamp.length() - 6);
    
    std::string branch_cmd = std::format("cd \"{}\" && git checkout -b \"{}\"", 
        cfg.project_env_path.string(), trim_branch);
    std::system(branch_cmd.c_str());

    // Discovery helper script
    fs::path helper_path = cfg.spip_root / "scripts" / "trim_helper.py";

    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    std::string analyze_cmd = std::format("\"{}\" \"{}\" \"{}\"", 
        python_bin.string(), helper_path.string(), script_path);
    
    std::string output = get_exec_output(analyze_cmd);
    std::set<std::string> needed_files;
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty()) needed_files.insert(fs::absolute(line).string());
    }

    // Always keep essential venv files
    needed_files.insert((cfg.project_env_path / "pyvenv.cfg").string());
    needed_files.insert((cfg.project_env_path / "bin" / "python").string());

    // Native dependencies (Mac/Linux support - D-14)
    std::vector<std::string> native_queue;
    for (const auto& f : needed_files) {
        if (f.ends_with(".so") || f.ends_with(".dylib")) native_queue.push_back(f);
    }

    size_t idx = 0;
    while(idx < native_queue.size()) {
        std::string lib = native_queue[idx++];
        // Use otool on Mac, ldd on Linux
        std::string dep_cmd = std::format("otool -L {} 2>/dev/null || ldd {} 2>/dev/null", quote_arg(lib), quote_arg(lib));
        std::string dep_out = get_exec_output(dep_cmd);
        std::stringstream ss2(dep_out);
        while(std::getline(ss2, line)) {
            // Match (otool style) /path/to/lib (compatibility...) OR (ldd style) lib => /path/to/lib (0x...)
            std::regex re(R"(\t([^\s]+) \(compatibility|=>\s+([^\s]+)\s+\()");
            std::smatch m;
            if (std::regex_search(line, m, re)) {
                std::string dep = m[1].matched ? m[1].str() : m[2].str();
                if (dep.find(cfg.project_env_path.string()) != std::string::npos) {
                    if (needed_files.find(dep) == needed_files.end()) {
                        needed_files.insert(dep);
                        native_queue.push_back(dep);
                    }
                }
            }
        }
    }

    std::cout << CYAN << "📦 Marking " << needed_files.size() << " essential files. Pruning others..." << RESET << std::endl;

    int pruned = 0;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_regular_file()) {
            std::string path = fs::absolute(entry.path()).string();
            if (needed_files.find(path) == needed_files.end() && path.find(".git") == std::string::npos) {
                fs::remove(entry.path());
                pruned++;
            }
        }
    }

    std::cout << GREEN << "✔️  Pruned " << pruned << " files. Testing environment..." << RESET << std::endl;
    
    std::string test_cmd = std::format("cd {} && ../spip run python {}", 
        quote_arg(cfg.current_project.string()), quote_arg(script_path));
    int ret = std::system(test_cmd.c_str());

    if (ret == 0) {
        std::cout << GREEN << "✨ Trim successful! Test passed." << RESET << std::endl;
        commit_state(cfg, "Trimmed environment for " + script_path);
    } else {
        std::cout << RED << "❌ Trim failed! Test did not pass. Reverting to previous state..." << RESET << std::endl;
        std::string revert_cmd = std::format("cd \"{}\" && git checkout -", cfg.project_env_path.string());
        std::system(revert_cmd.c_str());
    }
}

std::string parse_python_requirement(const std::string& req) {
    if (req.empty()) return "3";
    // Heuristic: check for mentioned versions and pick the most compatible installed one
    std::regex re(R"((\d+\.\d+))");
    std::smatch m;
    std::vector<std::string> mentioned;
    auto search = req;
    while (std::regex_search(search, m, re)) {
        mentioned.push_back(m[1].str());
        search = m.suffix().str();
    }
    
    // If no specific versions mentioned, default to a stable modern one
    if (mentioned.empty()) return "3.12"; 

    // Find highest mentioned version <= 3.13 (most stable for old stuff)
    std::string best = "3.12";
    for (const auto& v : mentioned) {
        if (v.starts_with("3.")) {
             if (v <= "3.13" && v > best) best = v;
        }
    }
    return best;
}

std::map<std::string, PackageInfo> resolve_only(const std::vector<std::string>& targets, const std::string& version = "", const std::string& target_py = "3.12") {
    std::vector<std::string> queue = targets;
    std::set<std::string> visited;
    std::map<std::string, PackageInfo> resolved;
    size_t i = 0;
    while(i < queue.size()) {
        std::string name = queue[i++];
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        std::replace(lower_name.begin(), lower_name.end(), '_', '-');
        std::replace(lower_name.begin(), lower_name.end(), '.', '-');
        
        if (visited.count(lower_name)) continue;
        
        PackageInfo info = (i == 1 && !version.empty()) ? get_package_info(name, version, target_py) : get_package_info(name, "", target_py);
        if (info.wheel_url.empty()) continue;
        
        resolved[lower_name + "-" + info.version] = info;
        visited.insert(lower_name);
        for (const auto& d : info.dependencies) queue.push_back(d);
    }
    return resolved;
}

int benchmark_concurrency(const Config& cfg) {
    std::cout << MAGENTA << "🔍 Benchmarking network for optimal download concurrency..." << RESET << std::endl;
    std::vector<int> tests = {1, 4, 8, 16, 32};
    // Use a small constant wheel for benchmarking
    std::string test_url = "https://files.pythonhosted.org/packages/ef/b5/b4b38202d659a11ff928174ad4ec0725287f3b89b88f343513a8dd645d94/idna-3.7-py3-none-any.whl";
    // If mirror is set, try to derive files URL or stick to PyPI for consistency if it's just for thread count calibration
    
    fs::path tmp = cfg.spip_root / "bench.whl";
    int best_c = 4;
    double min_time = 1e9;

    for (int c : tests) {
        if (c > (int)std::thread::hardware_concurrency() * 4) break;
        
        auto start = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        for (int i = 0; i < c; ++i) {
            workers.emplace_back([&, i]() {
                // Strict 4s timeout as per rule 0
                std::string cmd = std::format("timeout -s 9 4s curl -L -s {} -o {}_{}", test_url, tmp.string(), i);
                std::system(cmd.c_str());
            });
        }
        for (auto& t : workers) t.join();
        auto end = std::chrono::steady_clock::now();
        double d = std::chrono::duration<double>(end - start).count();
        std::cout << "  - " << std::format("{:2d}", c) << " threads: " << YELLOW << std::format("{:.4f}s", d) << RESET << std::endl;
        
        if (d > 0 && d < min_time) {
            min_time = d;
            best_c = c;
        }
        // Cleanup
        for (int i = 0; i < c; ++i) {
            fs::path p = std::format("{}_{}", tmp.string(), i);
            std::error_code ec;
            if (fs::exists(p, ec)) fs::remove(p, ec);
        }
    }
    std::cout << GREEN << "✨ Optimized download concurrency: " << best_c << RESET << std::endl;
    return best_c;
}

void parallel_download(const Config& cfg, const std::vector<PackageInfo>& info_list) {
    if (info_list.empty()) return;
    
    std::queue<PackageInfo> q;
    for (const auto& info : info_list) {
        fs::path target = cfg.spip_root / (info.name + "-" + info.version + ".whl");
        if (!fs::exists(target)) q.push(info);
    }
    
    if (q.empty()) {
        std::cout << GREEN << "✨ All wheels already cached." << RESET << std::endl;
        return;
    }

    int concurrency = benchmark_concurrency(cfg);
    std::cout << MAGENTA << "📥 Downloading " << q.size() << " unique wheels (concurrency: " << concurrency << ")..." << RESET << std::endl;

    std::mutex m;
    std::atomic<int> completed{0};
    int total = q.size();
    
    auto worker = [&]() {
        while (!g_interrupted) {
            PackageInfo info;
            {
                std::lock_guard<std::mutex> lock(m);
                if (q.empty()) return;
                info = q.front();
                q.pop();
            }
            
            fs::path target = cfg.spip_root / (info.name + "-" + info.version + ".whl");
            // Wrap in timeout to prevent stall on dead connections
            std::string cmd = std::format("timeout 300 curl -L --connect-timeout 10 --max-time 240 -s -# {} -o {}", quote_arg(info.wheel_url), quote_arg(target.string()));
            int ret = std::system(cmd.c_str());
            
            if (g_interrupted || ret != 0) {
                 if (fs::exists(target)) {
                     fs::remove(target); // Cleanup partial file
                 }
                 if (g_interrupted) return;
            }
            
            int c = ++completed;
            std::cout << "\rProgress: " << c << "/" << total << std::flush;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < concurrency; ++i) threads.emplace_back(worker);
    for (auto& t : threads) t.join();
    
    if (g_interrupted) {
        std::cout << std::endl << RED << "❌ Operation interrupted by user. Exiting." << RESET << std::endl;
        std::exit(1);
    }
    
    std::cout << std::endl << GREEN << "✔️  Parallel download complete." << RESET << std::endl;
}

void run_thread_test(const Config& cfg, int num_threads = -1) {
    int n = (num_threads > 0) ? num_threads : cfg.concurrency;
    std::cout << MAGENTA << "🧪 Benchmarking Concurrency Orchestration (" << n << " threads)..." << RESET << std::endl;
    
    std::string test_id = "bench_threads_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    TelemetryLogger* telemetry = cfg.telemetry ? new TelemetryLogger(cfg, test_id) : nullptr;
    if (telemetry) {
        std::cout << YELLOW << "📡 Telemetry active for benchmark..." << RESET << std::endl;
        telemetry->start();
    }

    auto start_wall = std::chrono::steady_clock::now();
    std::vector<std::thread> workers;
    std::atomic<int> running_count{0};
    std::atomic<int> total_completed{0};

    for (int i = 0; i < n; ++i) {
        workers.emplace_back([&, i]() {
            running_count++;
            // Busy work to saturate core
            volatile double sink = 0;
            for(long j = 0; j < 10000000; ++j) sink += (double)j * j;
            (void)sink;
            
            total_completed++;
            running_count--;
        });
    }

    // Monitor peak utilization
    bool monitoring = true;
    int peak_parallel = 0;
    std::thread monitor([&]() {
        while(monitoring) {
            int cur = running_count.load();
            if (cur > peak_parallel) peak_parallel = cur;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });

    for (auto& t : workers) t.join();
    monitoring = false;
    monitor.join();

    auto end_wall = std::chrono::steady_clock::now();
    double wall_sec = std::chrono::duration<double>(end_wall - start_wall).count();

    if (telemetry) {
        telemetry->stop();
        delete telemetry;
    }

    std::cout << "\n" << BOLD << GREEN << "🏁 Thread Test Results:" << RESET << std::endl;
    std::cout << "  - Target Threads:  " << n << std::endl;
    std::cout << "  - Peak Parallel:   " << BOLD << peak_parallel << RESET << std::endl;
    std::cout << "  - Total Wall Time: " << std::format("{:.3f}s", wall_sec) << std::endl;
    
    if (peak_parallel < n && n <= (int)std::thread::hardware_concurrency()) {
        std::cout << YELLOW << "⚠️ Warning: OS-level scheduling delay detected (Peak < Target)." << RESET << std::endl;
    } else if (peak_parallel == n) {
        std::cout << GREEN << "✔️  Hardware parallelism verified." << RESET << std::endl;
    }
}

std::string extract_exception(const std::string& output) {
    // Look for the last line of a traceback or a stand-alone Error/Exception
    std::regex err_re(R"(([a-zA-Z0-9_\.]+(Error|Exception):.*))");
    auto words_begin = std::sregex_iterator(output.begin(), output.end(), err_re);
    auto words_end = std::sregex_iterator();
    std::string last_err = "";
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        last_err = (*i)[1].str();
    }
    return last_err;
}

void matrix_test(const Config& cfg, const std::string& pkg, const std::string& custom_test_script = "", const std::string& python_version = "auto", bool profile = false, bool no_cleanup = false, int revision_limit = -1, bool test_all_revisions = false, bool vary_python = false, int pkg_revision_limit = 1) {
    if (vary_python) {
        std::cout << MAGENTA << "🧪 Starting Compatibility Test (Python Matrix) for " << BOLD << pkg << RESET << std::endl;
    } else {
        std::cout << MAGENTA << "🧪 Starting Build Server Mode (Matrix Test) for " << BOLD << pkg << RESET << std::endl;
    }
    if (profile) std::cout << YELLOW << "📊 Profiling mode enabled." << RESET << std::endl;
    
    std::vector<std::string> versions;
    if (vary_python) {
        // Hardcoded list of modern Python versions
        versions = {"3.13", "3.12", "3.11", "3.10", "3.9", "3.8", "3.7", "2.7"};
        if (revision_limit > 0) {
            if (versions.size() > static_cast<size_t>(revision_limit)) {
                versions.resize(revision_limit);
            }
        } else {
             if (versions.size() > 3) versions.resize(3);
        }

        // If checking previous versions of PACKAGE too
        if (pkg_revision_limit > 1) {
             std::vector<std::string> pkg_vers = get_all_versions(pkg);
             if (pkg_vers.size() > static_cast<size_t>(pkg_revision_limit)) {
                 pkg_vers.erase(pkg_vers.begin(), pkg_vers.begin() + (pkg_vers.size() - pkg_revision_limit));
             }
             // Create Cartesian product
             std::vector<std::string> combined;
             for (const auto& py : versions) {
                 for (const auto& pv : pkg_vers) {
                     combined.push_back(py + ":" + pv);
                 }
             }
             versions = combined;
        }

    } else {
        versions = get_all_versions(pkg);
    }

    if (versions.empty()) {
        std::cerr << RED << "❌ No versions found/selected for " << pkg << RESET << std::endl;
        return;
    }

    std::cout << BLUE << "📊 Found " << versions.size() << " " << (vary_python ? "python environments" : "versions") << ". ";

    if (!vary_python) {
        if (test_all_revisions) {
            std::cout << "Testing all..." << RESET << std::endl;
            // 'versions' vector is already complete, no slicing needed.
        } else if (revision_limit > 0) { // --limit N specified
            if (versions.size() > static_cast<size_t>(revision_limit)) {
                std::cout << "Limiting to last " << revision_limit << "..." << RESET << std::endl;
                // Keep the last 'revision_limit' versions.
                versions.erase(versions.begin(), versions.begin() + (versions.size() - revision_limit));
            } else {
                std::cout << "Testing all available (" << versions.size() << ") within limit." << RESET << std::endl;
                // All available versions are within the limit, so test all. No slicing needed.
            }
        } else { // Default case: no --all, no --limit specified.
            // Apply the heuristic of last 5 if total versions > 5.
            std::cout << "Testing all available (" << versions.size() << ")." << RESET << std::endl;
            if (versions.size() > 5) { 
                versions.erase(versions.begin(), versions.begin() + (versions.size() - 5));
            }
        }
    } else {
        std::cout << "Testing: ";
        for(const auto& v : versions) std::cout << v << " ";
        std::cout << RESET << std::endl;
    }
    
    // Initial analysis for the latest version to show config
    PackageInfo latest_info = get_package_info(pkg); // Gets metadata for latest version by default
    std::cout << CYAN << "📋 Configuration Info:" << RESET << std::endl;
    std::cout << "  - Package:         " << BOLD << pkg << RESET << std::endl;
    std::cout << "  - Latest Version:  " << latest_info.version << std::endl;
    std::cout << "  - Matrix Size:     " << versions.size() << (vary_python ? " combinations" : " versions to test") << std::endl;
    if (python_version != "auto") {
        std::cout << "  - Python Mode:     Manual Override (" << python_version << ")" << std::endl;
    } else {
        std::cout << "  - Python Mode:     Automatic (from metadata)" << std::endl;
    }

    std::string test_run_id = pkg + "_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    TelemetryLogger* telemetry = cfg.telemetry ? new TelemetryLogger(cfg, test_run_id) : nullptr;
    if (telemetry) {
        std::cout << YELLOW << "📡 Telemetry logging started (10 samples/sec)..." << RESET << std::endl;
        telemetry->start();
    }

    // Proceed to download and test if there are versions to process
    if (versions.empty()) {
        std::cout << YELLOW << "ℹ️ No versions selected for testing after applying filters." << RESET << std::endl;
        return;
    }


    ResourceProfiler* res_prof = profile ? new ResourceProfiler() : nullptr;
    std::map<std::string, PackageInfo> all_needed;
    std::mutex m_needed;
    std::vector<std::future<void>> futures;
    std::atomic<int> completed{0};
    // int total_res = versions.size(); // Unused

    auto task = [&](const std::string& ver) {
        // v_info logic inside resolve_only handles caching, but let's be safe
        std::map<std::string, PackageInfo> resolved;
        if (vary_python) {
             if (ver.find(':') != std::string::npos) {
                auto parts = split(ver, ':');
                resolved = resolve_only({pkg}, parts[1], parts[0]);
             } else {
                resolved = resolve_only({pkg}, "", ver);
             }
        } else {
             resolved = resolve_only({pkg}, ver, python_version);
        }

        std::lock_guard<std::mutex> l(m_needed);
        for (const auto& [id, info] : resolved) {
            all_needed[id] = info;
        }
        completed++;
    };

    for (const auto& ver : versions) {
        if (futures.size() >= (size_t)cfg.concurrency * 2) {
             // Simple throttle
             for(auto& f : futures) f.wait();
             futures.clear();
        }
        futures.push_back(std::async(std::launch::async, task, ver));
    }
    for(auto& f : futures) f.wait();
    if (profile && res_prof) {
        auto stats = res_prof->stop();
        std::cout << BLUE << "  [Profile] Resolution: " << stats.wall_time_seconds << "s wall, " << stats.cpu_time_seconds << "s CPU" << RESET << std::endl;
        delete res_prof;
    }
    
    std::vector<PackageInfo> info_list;
    for (const auto& [id, info] : all_needed) info_list.push_back(info);
    
    // In vary_python mode, we might need different info for different python versions if wheels differ.
    // For now we assume resolver fetches compatible ones for defaults; but actually parallel_download
    // just downloads based on the 'info' collected.
    // If vary_python is true, we haven't done pre-resolution for all python versions here (loop above uses versions which are just python strings).
    // Fix: In vary_python mode, pre-resolve for EACH python version to ensure we download correct wheels.
    // Fix: In vary_python mode, pre-resolve for EACH python version to ensure we download correct wheels.
    if (vary_python) {
        // Just rely on workers for now as before
    }

    res_prof = profile ? new ResourceProfiler(cfg.spip_root) : nullptr;
    parallel_download(cfg, info_list);
    if (profile && res_prof) {
        auto stats = res_prof->stop();
        std::cout << BLUE << "  [Profile] Download: " << stats.wall_time_seconds << "s wall, Disk delta: " << (stats.disk_delta_bytes / 1024) << " KB" << RESET << std::endl;
        delete res_prof;
    }

    // Git Storage for Wheels (User Request)
    if (!info_list.empty()) {
        std::string wheels_branch = "wheels";
        if (branch_exists(cfg, wheels_branch) || std::system(std::format("cd {} && git branch {}", quote_arg(cfg.repo_path.string()), wheels_branch).c_str()) == 0) {
             // Create a specific worktree for wheels if not exists
             fs::path wheel_wt = cfg.spip_root / "wheels_wt";
             if (!fs::exists(wheel_wt)) {
                 std::system(std::format("cd {} && git worktree add --detach {} {}", quote_arg(cfg.repo_path.string()), quote_arg(wheel_wt.string()), wheels_branch).c_str());
             }
             
             // Copy wheels
             for (const auto& info : info_list) {
                 fs::path cached = get_cached_wheel_path(cfg, info);
                 if (fs::exists(cached)) {
                     fs::copy_file(cached, wheel_wt / cached.filename(), fs::copy_options::overwrite_existing);
                 }
             }
             // Commit
             std::system(std::format("cd {} && git add . && git commit -m 'Add wheels' --quiet", quote_arg(wheel_wt.string())).c_str());
        }
    }

    fs::path test_script = custom_test_script;
    if (test_script.empty()) {
        std::cout << CYAN << "🤖 Generating minimal test script using Gemini..." << RESET << std::endl;
        fs::path gen_helper = cfg.spip_root / "scripts" / "generate_test.py";
        // Ensure the helper is there
        fs::path src_gen = fs::current_path() / "scripts" / "generate_test.py";
        if (fs::exists(src_gen)) fs::copy_file(src_gen, cfg.spip_root / "scripts" / "generate_test.py", fs::copy_options::overwrite_existing);
        
        std::string cmd = std::format("python3 {} {}", quote_arg(gen_helper.string()), quote_arg(pkg));
        std::string code = get_exec_output(cmd);
        if (code.empty() || code.find("Error") != std::string::npos || code.find("❌") != std::string::npos) {
            std::cout << YELLOW << "⚠️ LLM generation failed, returned error, or API key missing. Using robust fallback." << RESET << std::endl;
            std::string mod_pkg = pkg;
            std::replace(mod_pkg.begin(), mod_pkg.end(), '-', '_');
            code = "import " + mod_pkg + "\n" +
                   "print(f'Successfully imported " + mod_pkg + "')\n" +
                   "try:\n" +
                   "    import " + mod_pkg + ".utils\n" +
                   "    print('Successfully imported " + mod_pkg + ".utils')\n" +
                   "except ImportError: pass\n";
        }
        test_script = fs::current_path() / ("test_" + pkg + "_gen.py");
        std::ofstream os(test_script);
        os << code;
        os.close();
    }

    struct Result { std::string version; bool install; bool pkg_tests; bool custom_test; ResourceUsage stats; };
    std::vector<Result> results;
    
    // For AI summarization
    struct ErrorLog { std::string version; std::string python; std::string output; };
    std::vector<ErrorLog> error_logs;

    int total_vers = versions.size();

    fs::path state_file = cfg.envs_root / (".spip_" + std::string(vary_python ? "compat_" : "matrix_state_") + pkg + ".json");
    std::map<std::string, Result> state_results;
    
    // Load existing state
    if (fs::exists(state_file)) {
        std::ifstream ifs(state_file);
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            // Format: version|install|pkg_tests|custom_test|wall|cpu|mem|disk
            auto parts = split(line, '|');
            if (parts.size() >= 8) {
                Result r { parts[0], parts[1]=="1", parts[2]=="1", parts[3]=="1", 
                           {std::stod(parts[5]), (long)std::stoll(parts[6]), std::stod(parts[4]), (int64_t)std::stoll(parts[7])} };
                state_results[r.version] = r;
            }
        }
    }

    // Filter out already done versions
    std::vector<std::string> to_do;
    for (const auto& v : versions) {
        if (state_results.find(v) == state_results.end()) to_do.push_back(v);
        else results.push_back(state_results[v]);
    }

    if (to_do.empty() && !versions.empty()) {
        std::cout << GREEN << "✨ All selected versions already tested. Displaying cached results." << RESET << std::endl;
    } else {
        std::cout << BLUE << "📊 Versions to test: " << to_do.size() << " (already done: " << (versions.size() - to_do.size()) << ")" << RESET << std::endl;
    }

    // PARALLEL SETUP
    unsigned int max_threads = cfg.concurrency; if(max_threads==0) max_threads=4;
    std::cout << MAGENTA << "⚡ Parallel execution with " << max_threads << " threads." << RESET << std::endl;
    
    std::atomic<size_t> g_idx{0};
    std::mutex m_out, m_res, m_state, m_base_sync, m_git;
    std::vector<std::thread> workers;
    ErrorKnowledgeBase kb(cfg.db_file);
    
    auto worker_task = [&]() {
        while(!g_interrupted) {
            try {
                size_t task_i = g_idx.fetch_add(1);
                if(task_i >= to_do.size()) break;
                const auto& ver = to_do[task_i];
                
                { 
                    std::lock_guard<std::mutex> l(m_out);
                    std::cout << "\n" << BOLD << BLUE << "════════════════════════════════════════════════════════════" << RESET << std::endl;
                }
            
                Result res { ver, false, false, false, {0,0,0,0} };
                PackageInfo v_info;
                std::string target_py;

                if (vary_python) {
                    if (ver.find(':') != std::string::npos) {
                        auto parts = split(ver, ':');
                        target_py = parts[0];
                        v_info = get_package_info(pkg, parts[1]);
                    } else {
                        v_info = get_package_info(pkg); 
                        target_py = ver; 
                    }
                } else {
                    v_info = get_package_info(pkg, ver);
                    target_py = (python_version == "auto") ? parse_python_requirement(v_info.requires_python) : python_version;
                }

                {
                    std::lock_guard<std::mutex> l(m_out);
                    std::cout << BOLD << "🚀 Testing Version (" << (task_i + 1) << "/" << total_vers << "): " << GREEN << ver << RESET 
                              << " (Python " << YELLOW << target_py << RESET << ")" << std::endl;
                }

                std::string safe_ver = ver;
                for(char& c : safe_ver) if(!isalnum(c) && c!='.' && c!='-') c = '_';
                fs::path matrix_path = cfg.envs_root / ("matrix_" + cfg.project_hash + "_" + safe_ver);

                std::error_code ec;
                if(fs::exists(matrix_path, ec)) fs::remove_all(matrix_path, ec);
                ec.clear();

                std::unique_ptr<ResourceProfiler> v_prof = profile ? std::make_unique<ResourceProfiler>(matrix_path) : nullptr;
        

                // Setup/Refresh worktree
                std::string base_branch = "base/" + target_py;
                
                // Sync base branch creation
                bool exists = false;
                {
                    std::lock_guard<std::mutex> l(m_base_sync);
                    exists = branch_exists(cfg, base_branch);
                }
                
                if (!exists) {
                    {
                        std::lock_guard<std::mutex> l(m_out);
                        std::cout << YELLOW << "⚠️ Base version " << target_py << " not found. Attempting to bootstrap..." << RESET << std::endl;
                    }
                    std::lock_guard<std::mutex> l(m_base_sync);
                    if (!branch_exists(cfg, base_branch)) {
                        create_base_version(cfg, target_py);
                    }
                }

                // High-concurrency Git operations need throttling but not full serialization
                {
                    g_git_sem.acquire();
                    std::string wt_cmd = std::format("cd {} && git worktree add --detach {} {}", 
                         quote_arg(cfg.repo_path.string()), quote_arg(matrix_path.string()), quote_arg(base_branch));
                         
                    if (std::system(wt_cmd.c_str()) != 0) {
                         // Explicitly remove existing registration if "already exists" error occurred
                         std::string rm_cmd = std::format("cd {} && git worktree remove --force {} 2>/dev/null", 
                             quote_arg(cfg.repo_path.string()), quote_arg(matrix_path.string()));
                         std::system(rm_cmd.c_str());
                         std::system(std::format("cd {} && git worktree prune", quote_arg(cfg.repo_path.string())).c_str());
                         
                         // Retry
                         std::error_code ec_retry;
                         if (fs::exists(matrix_path, ec_retry)) fs::remove_all(matrix_path, ec_retry);
                         if (std::system(wt_cmd.c_str()) != 0) {
                             g_git_sem.release();
                             throw std::runtime_error("Failed to add git worktree even after cleanup.");
                         }
                    }
                    g_git_sem.release();
                }

        Config m_cfg = cfg;
        m_cfg.project_env_path = matrix_path;
        
        // 1. Install main package and its dependencies
        try {
            resolve_and_install(m_cfg, {pkg}, vary_python ? "" : ver, target_py); // Installs pkg and its runtime deps
            
            // Verify installation actually happened
            fs::path sp = get_site_packages(m_cfg);
            std::string p_name = pkg; 
            std::transform(p_name.begin(), p_name.end(), p_name.begin(), ::tolower);
            
            // Handle underscore/dash normalization for check
            std::string p_name_alt = p_name;
            std::replace(p_name_alt.begin(), p_name_alt.end(), '-', '_');
            
            bool installed = false;
            if (fs::exists(sp / p_name) || fs::exists(sp / (p_name + ".py"))) installed = true;
            else if (fs::exists(sp / p_name_alt) || fs::exists(sp / (p_name_alt + ".py"))) installed = true;
            
            // Fallback: Check for any .dist-info that starts with our safe name
            if (!installed) {
                for (const auto& entry : fs::directory_iterator(sp)) {
                    std::string entry_name = entry.path().filename().string();
                    std::transform(entry_name.begin(), entry_name.end(), entry_name.begin(), ::tolower);
                    std::replace(entry_name.begin(), entry_name.end(), '-', '_');
                    
                    if (entry_name.find(p_name_alt) == 0 && entry_name.find(".dist-info") != std::string::npos) {
                        installed = true;
                        break;
                    }
                }
            }
            
            if (installed) {
                res.install = true;
            } else {
                 std::cout << RED << "❌ Installation verification failed: " << pkg << " files not found." << RESET << std::endl;
                 res.install = false;
            }

        } catch (...) {
            std::cout << RED << "❌ Installation failed for " << ver << RESET << std::endl;
            res.install = false;
        }

        if (res.install) {
            // Check for development requirements for running tests
            fs::path dev_req_file = matrix_path / "requirements-dev.txt";
            if (fs::exists(dev_req_file)) {
                std::cout << CYAN << "📝 Installing development requirements from requirements-dev.txt..." << RESET << std::endl;
                std::vector<std::string> dev_targets;
                std::ifstream req_ifs(dev_req_file);
                std::string line;
                while (std::getline(req_ifs, line)) {
                    // Ignore empty lines and comments
                    line.erase(0, line.find_first_not_of(" \t\n\r"));
                    line.erase(line.find_last_not_of(" \t\n\r") + 1);
                    if (!line.empty() && !line.starts_with('#')) {
                        dev_targets.push_back(line);
                    }
                }
                if (!dev_targets.empty()) {
                    // Install dev dependencies into the same environment
                    // resolve_and_install will download necessary wheels if not cached
                    resolve_and_install(m_cfg, dev_targets, "", target_py);
                }
            }
            
            // 2. Run package tests
            std::cout << CYAN << "🧪 Running package tests..." << RESET << std::endl;
            fs::path python_bin = matrix_path / "bin" / "python";
            // Simple package test run
            std::string test_cmd = std::format("{} -m pytest --version >/dev/null 2>&1", quote_arg(python_bin.string()));
            if (std::system(test_cmd.c_str()) != 0) {
                 std::system(std::format("{} -m pip install pytest >/dev/null 2>&1", quote_arg(python_bin.string())).c_str());
            }
            
            // Try to find package dir for tests
            fs::path sp = get_site_packages(m_cfg);
            std::string p_name = pkg; std::transform(p_name.begin(), p_name.end(), p_name.begin(), ::tolower);
            fs::path p_path = sp / p_name;
             
             // REWRITE ENTIRE BLOCK WITH CLEAN LOOP
             if (fs::exists(p_path)) {
                 std::string pytest_flags = "--maxfail=1 -q";
                 bool success = false;
                 
                 std::set<std::string> applied_fixes; // Track fixes to prevent loops
                 for (int attempt = 0; attempt < 10; ++attempt) { // Increased attempt count to allow for chain reaction fixes
                     std::string pytest_cmd = std::format("{} -m pytest {} {}", quote_arg(python_bin.string()), quote_arg(p_path.string()), pytest_flags);
                     std::string test_out = get_exec_output(pytest_cmd + " 2>&1"); // Ensure we capture everything
                     
                     // Helper lambda to run clean sys for final result check
                     auto run_final = [&]() {
                         return std::system(pytest_cmd.c_str()) == 0;
                     };

                      // Knowledge Base Lookup
                      std::string exc_found = extract_exception(test_out);
                      bool action_taken = false;
                      if (!exc_found.empty()) {
                          std::string learned_fix = kb.lookup_fix(exc_found);
                          // Only apply if we haven't tried this specific fix in this run yet
                          if (!learned_fix.empty() && applied_fixes.find(learned_fix) == applied_fixes.end()) {
                              std::cout << BLUE << "💡 Found learned fix for \"" << exc_found << "\": " << GREEN << learned_fix << RESET << std::endl;
                              bool fix_applied = false;
                              if (learned_fix.starts_with("install ")) {
                                  try { 
                                      resolve_and_install(m_cfg, {learned_fix.substr(8)}, "", target_py); 
                                      fix_applied = true; 
                                  } catch (...) {}
                              } else if (learned_fix == "Inject sitecustomize.py shim") {
                                  fs::path sp = get_site_packages(m_cfg);
                                  if (!sp.empty()) {
                                      std::ofstream ofs(sp / "sitecustomize.py", std::ios::app);
                                      ofs << "\nimport collections\nimport collections.abc\n"
                                          << "for _n in ['Mapping', 'MutableMapping', 'Sequence', 'MutableSequence', 'Iterable', 'MutableSet', 'Callable', 'Set']:\n"
                                          << "    if not hasattr(collections, _n) and hasattr(collections.abc, _n): setattr(collections, _n, getattr(collections.abc, _n))\n";
                                      fix_applied = true;
                                  }
                              }
                              if (fix_applied) {
                                  action_taken = true;
                                  applied_fixes.insert(learned_fix);
                              }
                          }
                      }

                      // Regexes
                      std::regex import_err_re(R"(ImportError: ([a-zA-Z0-9_\-]+) is a required dependency)");
                      std::regex mod_err_re(R"(ModuleNotFoundError: No module named '?([a-zA-Z0-9_\-]+)'?)");
                      std::regex rel_import_re(R"(ImportError: attempted relative import with no known parent package)");
                      // Legacy collections patch for Python 3.10+
                      std::regex collections_re(R"(ImportError: cannot import name '([a-zA-Z]+)' from 'collections')");
                      std::smatch m;

                      // Regexes
                      // ... (removed redundant bool action_taken)

                      if (std::regex_search(test_out, m, collections_re)) {
                          std::string name = m[1].str();
                          std::string exc_found = extract_exception(test_out);
                          std::cout << YELLOW << "🩹 Detected legacy 'collections." << name << "' issue. Injecting compatibility shim..." << RESET << std::endl;
                          fs::path site_packages = get_site_packages(m_cfg);
                          if (!site_packages.empty()) {
                              fs::path sc_path = site_packages / "sitecustomize.py";
                              std::ofstream ofs(sc_path, std::ios::app);
                              ofs << "\nimport collections\nimport collections.abc\n";
                              ofs << "for _n in ['Mapping', 'MutableMapping', 'Sequence', 'MutableSequence', 'Iterable', 'MutableSet', 'Callable', 'Set']:\n";
                              ofs << "    if not hasattr(collections, _n) and hasattr(collections.abc, _n):\n";
                              ofs << "        setattr(collections, _n, getattr(collections.abc, _n))\n";
                              action_taken = true;
                              kb.store(pkg, target_py, exc_found, "Inject sitecustomize.py shim");
                          }
                      }

                      if (!action_taken && (std::regex_search(test_out, m, import_err_re) || std::regex_search(test_out, m, mod_err_re))) {
                         std::string missing_pkg = m[1].str();
                         std::string exc_found = extract_exception(test_out);
                         
                         if (missing_pkg == "distutils") missing_pkg = "setuptools";
                         if (missing_pkg == "path") missing_pkg = "path.py"; // Common confusion, or 'jaraco.path' ? 'path' usually refers to 'path.py' on PyPI or just 'path'

                         if (missing_pkg.find('.') == std::string::npos && missing_pkg != p_name) {
                             // Check repetition
                             std::string fix_key = "install " + missing_pkg;
                             if (applied_fixes.find(fix_key) == applied_fixes.end()) {
                                 std::cout << YELLOW << "⚠️ Missing test dependency: " << missing_pkg << ". Installing..." << RESET << std::endl;
                                 try {
                                     resolve_and_install(m_cfg, {missing_pkg}, "", target_py);
                                     action_taken = true;
                                     applied_fixes.insert(fix_key);
                                     kb.store(pkg, target_py, exc_found, fix_key);
                                 } catch (...) {
                                     std::cout << RED << "❌ Failed to install " << missing_pkg << RESET << std::endl;
                                 }
                             }
                         }
                     } 
                     
                     if (!action_taken && std::regex_search(test_out, m, rel_import_re)) {
                          if (pytest_flags.find("--import-mode=importlib") == std::string::npos) {
                              std::cout << YELLOW << "⚠️ Relative import error. Adding --import-mode=importlib..." << RESET << std::endl;
                              pytest_flags += " --import-mode=importlib";
                              action_taken = true;
                          }
                     }

                      if (!action_taken) {
                          // Try running from worktree root if site-packages run was empty or failed early
                          if (test_out.find("no tests ran") != std::string::npos || (attempt == 0 && !success)) {
                              std::string root_pytest_cmd = std::format("{} -m pytest {} {}", quote_arg(python_bin.string()), quote_arg(matrix_path.string()), pytest_flags);
                              std::string root_test_out = get_exec_output(root_pytest_cmd + " 2>&1");
                              if (root_test_out.find("no tests ran") == std::string::npos && root_test_out.find("ERROR") == std::string::npos) {
                                  std::cout << GREEN << "✨ Tests found in worktree root. Retrying from there..." << RESET << std::endl;
                                  test_out = root_test_out;
                                  pytest_cmd = root_pytest_cmd;
                                  // Re-check regexes against new output in next iteration if needed,
                                  // but for now let's just use this as the primary result.
                              }
                          }
                          
                          // No recoverable error found, or tests passed/failed on other grounds
                          // Run standard system for result
                          success = run_final();
                          break;
                      } 
                     // If action taken, loop continues to retry
                 }
                 res.pkg_tests = success;
             } else {
                 std::cout << YELLOW << "⚠️ Could not find package dir for tests. Skipping pkg tests." << RESET << std::endl;
             }

            // 3. Run custom/gen test with self-healing
            {
                std::lock_guard<std::mutex> l(m_out);
                std::cout << CYAN << "🧪 Running custom test script..." << RESET << std::endl;
            }
            
            bool custom_success = false;
            std::string last_custom_out = "";
            for (int attempt = 0; attempt < 3; ++attempt) {
                std::string run_custom = std::format("{} {}", quote_arg(python_bin.string()), quote_arg(test_script.string()));
                std::string custom_out = get_exec_output(run_custom + " 2>&1");
                last_custom_out = custom_out;
                
                std::string exc_found_kb = extract_exception(custom_out);
                bool action_taken = false;
                if (!exc_found_kb.empty()) {
                    std::string learned_fix = kb.lookup_fix(exc_found_kb);
                    if (!learned_fix.empty()) {
                        std::cout << BLUE << "💡 Found learned fix for \"" << exc_found_kb << "\": " << GREEN << learned_fix << RESET << std::endl;
                        if (learned_fix.starts_with("install ")) {
                            try { resolve_and_install(m_cfg, {learned_fix.substr(8)}, "", target_py); action_taken = true; } catch (...) {}
                        } else if (learned_fix == "Inject sitecustomize.py shim") {
                              fs::path sp = get_site_packages(m_cfg);
                              if (!sp.empty()) {
                                  std::ofstream ofs(sp / "sitecustomize.py", std::ios::app);
                                  ofs << "\nimport collections\nimport collections.abc\n"
                                      << "for _n in ['Mapping', 'MutableMapping', 'Sequence', 'MutableSequence', 'Iterable', 'MutableSet', 'Callable', 'Set']:\n"
                                      << "    if not hasattr(collections, _n) and hasattr(collections.abc, _n): setattr(collections, _n, getattr(collections.abc, _n))\n";
                                  action_taken = true;
                              }
                        }
                    }
                }
                std::regex mod_err_re(R"(ModuleNotFoundError: No module named '?([a-zA-Z0-9_\-]+)'?)");
                std::regex collections_re(R"(ImportError: cannot import name '([a-zA-Z]+)' from 'collections')");
                std::smatch m;

                if (std::regex_search(custom_out, m, collections_re)) {
                    std::string name = m[1].str();
                    std::string exc_found = extract_exception(custom_out);
                    std::cout << YELLOW << "🩹 [CustomTest] Detected legacy 'collections." << name << "' issue. Injecting shim..." << RESET << std::endl;
                    fs::path sp = get_site_packages(m_cfg);
                    if (!sp.empty()) {
                        fs::path sc_path = sp / "sitecustomize.py";
                        std::ofstream ofs(sc_path, std::ios::app);
                        ofs << "\nimport collections\nimport collections.abc\n";
                        ofs << "for _n in ['Mapping', 'MutableMapping', 'Sequence', 'MutableSequence', 'Iterable', 'MutableSet', 'Callable', 'Set']:\n";
                        ofs << "    if not hasattr(collections, _n) and hasattr(collections.abc, _n):\n";
                        ofs << "        setattr(collections, _n, getattr(collections.abc, _n))\n";
                        action_taken = true;
                        kb.store(pkg, target_py, exc_found, "Inject sitecustomize.py shim");
                    }
                }

                if (!action_taken && std::regex_search(custom_out, m, mod_err_re)) {
                    std::string missing_pkg = m[1].str();
                    std::string exc_found = extract_exception(custom_out);
                    if (missing_pkg == "distutils") missing_pkg = "setuptools";
                    if (missing_pkg == "path") missing_pkg = "path.py";
                    if (missing_pkg != p_name) {
                        std::cout << YELLOW << "⚠️ [CustomTest] Missing dependency: " << missing_pkg << ". Installing..." << RESET << std::endl;
                        try { 
                            resolve_and_install(m_cfg, {missing_pkg}, "", target_py); 
                            action_taken = true;  
                            kb.store(pkg, target_py, exc_found, "install " + missing_pkg);
                        } catch (...) {}
                    }
                }

                if (custom_out.find("Traceback") == std::string::npos && custom_out.find("Error:") == std::string::npos) {
                    {
                        std::lock_guard<std::mutex> l(m_out);
                        std::cout << custom_out << (custom_out.empty() ? "" : "\n");
                    }
                    custom_success = true;
                    break;
                }
                
                if (!action_taken) {
                    std::lock_guard<std::mutex> l(m_out);
                    std::cout << custom_out << "\n";
                    break;
                }
            }
            res.custom_test = custom_success;
            if (!custom_success) {
                std::lock_guard<std::mutex> l(m_res);
                std::string exc_found = extract_exception(last_custom_out);
                if (!exc_found.empty()) kb.store(pkg, target_py, exc_found, "");
                // error_logs.push_back({ver, target_py, "Custom test failed after retries"});
            }
        }

        if (profile && v_prof) {
            res.stats = v_prof->stop();
        }

        {
            std::lock_guard<std::mutex> l(m_res);
            results.push_back(res);
            
            // Atomically update state file
            std::lock_guard<std::mutex> ls(m_state);
            std::ofstream ofs(state_file, std::ios::app);
            ofs << res.version << "|" << (res.install ? "1" : "0") << "|" << (res.pkg_tests ? "1" : "0") << "|" << (res.custom_test ? "1" : "0") << "|"
                << res.stats.wall_time_seconds << "|" << res.stats.cpu_time_seconds << "|" << res.stats.peak_memory_kb << "|" << res.stats.disk_delta_bytes << "\n";
        }

        // Thread-local cleanup necessary for parallel unique paths
        if (!no_cleanup) {
             g_git_sem.acquire();
             std::string rm_cmd = std::format("cd {} && git worktree remove --force {} 2>/dev/null", quote_arg(cfg.repo_path.string()), quote_arg(matrix_path.string()));
             std::system(rm_cmd.c_str());
             g_git_sem.release();
        }
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> l(m_out);
                std::cerr << RED << "🔥 Worker exception: " << e.what() << RESET << std::endl;
            } catch (...) {
                std::lock_guard<std::mutex> l(m_out);
                std::cerr << RED << "🔥 Worker unknown exception" << RESET << std::endl;
            }
        }
    }; // end lambda

    // Launch workers
    for(unsigned int i=0; i<max_threads; ++i) workers.emplace_back(worker_task);
    
    // Join all
    for(auto& t : workers) t.join();

    // Global cleanup
    if (!no_cleanup) {
        std::cout << MAGENTA << "🧹 Pruning worktree metadata..." << RESET << std::endl;
        std::system(std::format("cd {} && git worktree prune", quote_arg(cfg.repo_path.string())).c_str());
    } else {
        std::cout << BLUE << "ℹ️ Skipping final cleanup (reusable worktree preserved)." << RESET << std::endl;
    }

    // AI Summarization Phase
    const char* api_key = std::getenv("GEMINI_API_KEY");
    if (api_key && !error_logs.empty()) {
        std::cout << MAGENTA << "🤖 Asking AI to summarize the failures..." << RESET << std::endl;
        fs::path log_json = cfg.spip_root / "matrix_errors.json";
        std::ofstream ofs(log_json);
        ofs << "[\n";
        for (size_t i = 0; i < error_logs.size(); ++i) {
            ofs << "  {\n";
            ofs << "    \"version\": \"" << error_logs[i].version << "\",\n";
            ofs << "    \"python\": \"" << error_logs[i].python << "\",\n";
            // Escape quotes and newlines for JSON
            std::string escaped = "";
            for (char c : error_logs[i].output) {
                if (c == '\"') escaped += "\\\"";
                else if (c == '\n') escaped += "\\n";
                else if (c == '\\') escaped += "\\\\";
                else if (c < 32) {} // Skip other control chars
                else escaped += c;
            }
            ofs << "    \"output\": \"" << escaped << "\"\n";
            ofs << "  }" << (i == error_logs.size() - 1 ? "" : ",") << "\n";
        }
        ofs << "]";
        ofs.close();

        fs::path summary_script = cfg.spip_root / "scripts" / "summarize_errors.py";
        // Ensure the script is in spip_root
        fs::path src_sum = fs::current_path() / "scripts" / "summarize_errors.py";
        if (fs::exists(src_sum)) fs::copy_file(src_sum, summary_script, fs::copy_options::overwrite_existing);

        std::system(std::format("python3 {} {}", quote_arg(summary_script.string()), quote_arg(log_json.string())).c_str());
    }

    std::cout << "\n" << BOLD << MAGENTA << "🏁 Matrix Test Summary for " << pkg << RESET << std::endl;
    if (profile) {
        std::cout << std::format("{:<15} {:<10} {:<15} {:<15} {:<15} {:<15}", "Version", "Install", "Pkg Tests", "Custom Test", "Wall Time", "CPU Time") << std::endl;
        std::cout << "--------------------------------------------------------------------------------------------" << std::endl;
    } else {
        std::cout << std::format("{:<15} {:<10} {:<15} {:<15}", "Version", "Install", "Pkg Tests", "Custom Test") << std::endl;
        std::cout << "------------------------------------------------------------" << std::endl;
    }
    
    for (const auto& r : results) {
        if (profile) {
            std::cout << std::format("{:<15} {:<19} {:<24} {:<24} {:<15.2f} {:<15.2f}", 
                r.version, 
                (r.install ? GREEN + "PASS" : RED + "FAIL") + RESET,
                (r.pkg_tests ? GREEN + "PASS" : YELLOW + "FAIL/SKIP") + RESET,
                (r.custom_test ? GREEN + "PASS" : RED + "FAIL") + RESET,
                r.stats.wall_time_seconds,
                r.stats.cpu_time_seconds) << std::endl;
        } else {
            std::cout << std::format("{:<15} {:<19} {:<24} {:<24}", 
                r.version, 
                (r.install ? GREEN + "PASS" : RED + "FAIL") + RESET,
                (r.pkg_tests ? GREEN + "PASS" : YELLOW + "FAIL/SKIP") + RESET,
                (r.custom_test ? GREEN + "PASS" : RED + "FAIL") + RESET) << std::endl;
        }
    }

    auto learned_fixes = kb.get_fixes_for_pkg(pkg);
    if (!learned_fixes.empty()) {
        std::cout << "\n" << BOLD << BLUE << "💡 Suggested Fixes (from Knowledge Base for " << pkg << "):" << RESET << std::endl;
        std::set<std::string> seen_fixes;
        for (const auto& f : learned_fixes) {
            std::string key = f.first + " -> " + f.second;
            if (f.second != "" && seen_fixes.find(key) == seen_fixes.end()) {
                std::cout << "  - " << YELLOW << f.first << RESET << " -> " << GREEN << f.second << RESET << std::endl;
                seen_fixes.insert(key);
            }
        }
    }

    if (telemetry) {
        telemetry->stop();
        delete telemetry;
        std::cout << YELLOW << "📡 Telemetry logging stopped. Database updated: " << (cfg.spip_root / "telemetry.db") << RESET << std::endl;
    }
}

struct TopPkg { std::string name; long downloads; };

void show_top_packages(bool show_references, bool show_dependencies) {
    if (show_references) {
        std::cout << MAGENTA << "🏆 Fetching Top 10 PyPI Packages by Dependent Repos (Libraries.io)..." << RESET << std::endl;
        // Libraries.io HTML parsing via regex (Follow redirects with -L)
        std::string cmd = "curl -L -s -H \"User-Agent: Mozilla/5.0\" \"https://libraries.io/search?languages=Python&order=desc&platforms=Pypi&sort=dependents_count\"";
        std::string html = get_exec_output(cmd);
        
        bool success = false;
        if (!html.empty() && html.find("Login to Libraries.io") == std::string::npos) {
            std::cout << BOLD << std::format("{:<5} {:<30}", "Rank", "Package") << RESET << std::endl;
            std::cout << "-----------------------------------" << std::endl;

            std::regex re(R"(<h5>\s*<a href=\"/pypi/[^\"]+\">([^<]+)</a>)");
            std::sregex_iterator next(html.begin(), html.end(), re);
            std::sregex_iterator end;
            int rank = 1;
            while (next != end && rank <= 10) {
                 std::smatch match = *next;
                 std::cout << std::format("{:<5} {:<30}", rank++, match[1].str()) << std::endl;
                 ++next;
            }
            if (rank > 1) success = true;
        }
        
        if (!success) {
            std::cout << YELLOW << "⚠️  Unable to scrape Libraries.io (Login required or structure changed)." << RESET << std::endl;
            std::cout << YELLOW << "   Falling back to Top PyPI Download Statistics..." << RESET << std::endl;
            show_top_packages(false, false);
        }
    } else if (show_dependencies) {
        std::cout << MAGENTA << "🏆 Fetching Top 10 PyPI Packages by Dependency Count..." << RESET << std::endl;
        
        // Fetch top packages by download count as a base list
        std::string cmd_download = "curl -s \"https://hugovk.github.io/top-pypi-packages/top-pypi-packages-30-days.json\"";
        std::string json_download = get_exec_output(cmd_download);
        
        std::vector<std::pair<std::string, long>> top_downloaded;
        size_t pos = 0;
        int count = 0;
        while (count < 100 && pos < json_download.length()) { // Fetch top 100 for analysis
            size_t proj_key = json_download.find("\"project\":", pos);
            if (proj_key == std::string::npos) break;
            size_t start_q = json_download.find("\"", proj_key + 10);
            size_t end_q = json_download.find("\"", start_q + 1);
            std::string name = json_download.substr(start_q + 1, end_q - start_q - 1);
            
            size_t dl_key = json_download.find("\"download_count\":", end_q);
            size_t end_val = json_download.find_first_of(",}", dl_key);
            std::string dl_str = json_download.substr(dl_key + 17, end_val - (dl_key + 17));
            long dl = std::stol(dl_str);
            
            top_downloaded.push_back({name, dl});
            pos = end_val;
            count++;
        }

        std::vector<std::pair<std::string, int>> dep_counts;
        std::cout << "Analyzing dependencies for top packages..." << std::endl;
        int analyzed = 0;
        for (const auto& pair : top_downloaded) {
            if (analyzed >= 50) break; // Limit analysis to top 50 downloaded for performance
            
            PackageInfo info = get_package_info(pair.first); // Fetch metadata to count dependencies
            if (!info.name.empty()) {
                dep_counts.push_back({info.name, info.dependencies.size()});
                analyzed++;
                if (analyzed % 10 == 0) std::cout << "\rAnalyzed " << analyzed << "/50..." << std::flush;
            }
        }
        std::cout << std::endl;

        std::sort(dep_counts.begin(), dep_counts.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        std::cout << BOLD << std::format("{:<5} {:<30} {:<15}", "Rank", "Package", "Dependencies") << RESET << std::endl;
        std::cout << "----------------------------------------------------" << std::endl;
        for (int i = 0; i < std::min((int)dep_counts.size(), 10); ++i) {
            std::cout << std::format("{:<5} {:<30} {:<15}", i + 1, dep_counts[i].first, dep_counts[i].second) << std::endl;
        }

    } else {
        std::cout << MAGENTA << "🏆 Fetching Top 10 PyPI Packages by Downloads (30 days)..." << RESET << std::endl;
        std::string cmd = "curl -s \"https://hugovk.github.io/top-pypi-packages/top-pypi-packages-30-days.json\"";
        std::string json = get_exec_output(cmd);
        
        std::cout << BOLD << std::format("{:<5} {:<30} {:<15}", "Rank", "Package", "Downloads") << RESET << std::endl;
        std::cout << "----------------------------------------------------" << std::endl;

        // Simple manual parsing to avoid heavy deps
        size_t pos = 0;
        int rank = 1;
        while (rank <= 10) {
            size_t proj_key = json.find("\"project\":", pos);
            if (proj_key == std::string::npos) break;
            size_t start_q = json.find("\"", proj_key + 10);
            size_t end_q = json.find("\"", start_q + 1);
            std::string name = json.substr(start_q + 1, end_q - start_q - 1);
            
            size_t dl_key = json.find("\"download_count\":", end_q);
            size_t end_val = json.find_first_of(",}", dl_key);
            std::string dl_str = json.substr(dl_key + 17, end_val - (dl_key + 17));
            long dl = std::stol(dl_str);

            std::cout << std::format("{:<5} {:<30} {:<15}", rank++, name, dl) << std::endl;
            pos = end_val;
        }
    }
}

void bundle_package(const Config& cfg, const std::string& path) {
    fs::path target_dir = fs::absolute(path);
    if (!fs::exists(target_dir) || !fs::is_directory(target_dir)) {
        std::cerr << RED << "❌ Target directory not found: " << path << RESET << std::endl;
        return;
    }

    std::string pkg_name = target_dir.filename().string();
    std::cout << MAGENTA << "📦 Bundling C++23 package '" << pkg_name << "' from " << target_dir.string() << "..." << RESET << std::endl;

    // 1. Generate setup.py if it doesn't exist
    fs::path setup_path = target_dir / "setup.py";
    if (!fs::exists(setup_path)) {
        std::cout << CYAN << "📝 Generating setup.py..." << RESET << std::endl;
        std::vector<std::string> cpp_files;
        std::vector<std::string> py_files;
        std::string test_file;
        
        for (const auto& entry : fs::directory_iterator(target_dir)) {
            if (entry.path().extension() == ".cpp") {
                cpp_files.push_back(entry.path().filename().string());
            } else if (entry.path().extension() == ".py") {
                std::string fname = entry.path().filename().string();
                if (fname != "setup.py") {
                    if (fname.find("test") != std::string::npos) test_file = fname;
                    else py_files.push_back(fname.substr(0, fname.length() - 3));
                }
            }
        }

        if (cpp_files.empty()) {
             std::cerr << RED << "❌ No .cpp files found in " << target_dir.string() << RESET << std::endl;
             return;
        }

        std::ofstream os(setup_path);
        os << "from setuptools import setup, Extension\n";
        os << "import os\n\n";
        os << "module = Extension('" << pkg_name << "_cpp',\n";
        os << "    sources=[";
        for (size_t i = 0; i < cpp_files.size(); ++i) {
            os << "'" << cpp_files[i] << "'" << (i == cpp_files.size() - 1 ? "" : ", ");
        }
        os << "],\n";
        os << "    extra_compile_args=['-std=c++23']\n";
        os << ")\n\n";
        os << "setup(\n";
        os << "    name='" << pkg_name << "',\n";
        os << "    version='0.1',\n";
        os << "    ext_modules=[module],\n";
        os << "    py_modules=[";
        for (size_t i = 0; i < py_files.size(); ++i) {
            os << "'" << py_files[i] << "'" << (i == py_files.size() - 1 ? "" : ", ");
        }
        os << "],\n";
        os << ")\n";
        os.close();
        std::cout << GREEN << "✔️  Created setup.py." << RESET << std::endl;
    }

    // 2. Install into current environment
    setup_project_env(cfg);
    
    fs::path python_bin = cfg.project_env_path / "bin" / "python";
    
    // Ensure pip is installed
    std::string check_pip = std::format("{} -m pip --version >/dev/null 2>&1", quote_arg(python_bin.string()));
    if (std::system(check_pip.c_str()) != 0) {
        std::cout << YELLOW << "⚠️ pip not found. Installing via ensurepip..." << RESET << std::endl;
        std::string install_pip = std::format("{} -m ensurepip --upgrade", quote_arg(python_bin.string()));
        std::system(install_pip.c_str());
    }

    std::cout << BLUE << "🚀 Installing package..." << RESET << std::endl;
    std::string install_cmd = std::format("cd {} && {} -m pip install .", 
        quote_arg(target_dir.string()), quote_arg(python_bin.string()));
    
    int ret = std::system(install_cmd.c_str());
    if (ret != 0) {
        std::cerr << RED << "❌ Installation failed." << RESET << std::endl;
        return;
    }
    std::cout << GREEN << "✔️  Package installed successfully." << RESET << std::endl;

    // 3. Run test if found
    std::string test_file;
    for (const auto& entry : fs::directory_iterator(target_dir)) {
        if (entry.path().extension() == ".py" && entry.path().filename().string().find("test") != std::string::npos) {
            test_file = entry.path().filename().string();
            break;
        }
    }

    if (!test_file.empty()) {
        std::cout << MAGENTA << "🧪 Running test: " << test_file << "..." << RESET << std::endl;
        std::string test_cmd = std::format("cd {} && {} {}", 
            quote_arg(target_dir.string()), quote_arg(python_bin.string()), quote_arg(test_file));
        std::system(test_cmd.c_str());
    } else {
        std::cout << YELLOW << "⚠️ No test file found (looked for *test*.py)." << RESET << std::endl;
    }
}

uintmax_t get_dir_size(const fs::path& p) {
    uintmax_t size = 0;
    std::error_code ec;
    if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
        try {
            for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (fs::is_regular_file(*it, ec)) {
                    uintmax_t s = fs::file_size(*it, ec);
                    if (!ec) size += s;
                }
                ec.clear();
            }
        } catch (...) {}
    }
    return size;
}

void show_usage_stats(const Config& cfg) {
    uintmax_t repo_size = get_dir_size(cfg.repo_path);
    uintmax_t envs_size = get_dir_size(cfg.envs_root);
    uintmax_t db_size = get_dir_size(cfg.spip_root / "db");
    uintmax_t total_size = get_dir_size(cfg.spip_root);

    std::cout << BOLD << "📊 Disk Usage Statistics:" << RESET << std::endl;
    std::cout << "  - Git Repository: " << CYAN << (repo_size / (1024 * 1024)) << " MB" << RESET << std::endl;
    std::cout << "  - Environments:   " << CYAN << (envs_size / (1024 * 1024)) << " MB" << RESET << std::endl;
    std::cout << "  - Package DB:     " << CYAN << (db_size / (1024 * 1024)) << " MB" << RESET << std::endl;
    std::cout << "  - Total Vault:    " << BOLD << GREEN << (total_size / (1024 * 1024)) << " MB" << RESET << std::endl;
}

void cleanup_spip(Config& cfg, bool remove_all = false) {
    std::cout << MAGENTA << "🧹 Starting cleanup of .spip directory..." << RESET << std::endl;
    show_usage_stats(cfg);

    // 1. Clean up environments
    if (fs::exists(cfg.envs_root)) {
        for (const auto& entry : fs::directory_iterator(cfg.envs_root)) {
            if (entry.is_directory()) {
                fs::path origin_file = entry.path() / ".project_origin";
                bool should_remove = remove_all;
                std::string project_path;
                
                if (!remove_all) {
                    if (fs::exists(origin_file)) {
                        std::ifstream ifs(origin_file);
                        std::getline(ifs, project_path);
                        if (!project_path.empty() && !fs::exists(project_path)) {
                            should_remove = true;
                            std::cout << YELLOW << "  - Removing orphaned environment: " << project_path << RESET << std::endl;
                        } else {
                            // Check if unused for 30 days
                            auto last_write = fs::last_write_time(entry.path());
                            auto now = std::chrono::file_clock::now();
                            auto age = now - last_write;
                            if (age > std::chrono::hours(24 * 30)) {
                                should_remove = true;
                                std::cout << YELLOW << "  - Removing unused environment (30+ days old): " << project_path << RESET << std::endl;
                            }
                        }
                    } else {
                        // No origin file - might be a broken environment
                        should_remove = true;
                        std::cout << YELLOW << "  - Removing broken environment: " << entry.path().filename().string() << RESET << std::endl;
                    }
                } else {
                    if (fs::exists(origin_file)) {
                        std::ifstream ifs(origin_file);
                        std::getline(ifs, project_path);
                    }
                    std::cout << YELLOW << "  - Removing environment: " << (project_path.empty() ? entry.path().filename().string() : project_path) << RESET << std::endl;
                }

                if (should_remove) {
                    std::string hash = entry.path().filename().string();
                    
                    // Remove worktree
                    std::string wt_cmd = std::format("cd {} && git worktree remove --force {} 2>/dev/null", 
                        quote_arg(cfg.repo_path.string()), quote_arg(entry.path().string()));
                    std::system(wt_cmd.c_str());

                    // Remove branch
                    std::string br_cmd = std::format("cd {} && git branch -D project/{} 2>/dev/null", 
                        quote_arg(cfg.repo_path.string()), quote_arg(hash));
                    std::system(br_cmd.c_str());

                    // Ensure directory is gone
                    if (fs::exists(entry.path())) {
                        fs::remove_all(entry.path());
                    }
                }
            }
        }
    }

    // 2. Remove temporary files and unrecognized scripts
    std::cout << MAGENTA << "🗑 Removing temporary files and caches..." << RESET << std::endl;
    if (fs::exists(cfg.spip_root)) {
        for (const auto& entry : fs::directory_iterator(cfg.spip_root)) {
            std::string name = entry.path().filename().string();
            if (name.starts_with("temp_venv_")) {
                std::cout << YELLOW << "  - Removing " << name << RESET << std::endl;
                fs::remove_all(entry.path());
            } else if (entry.is_regular_file()) {
                if (name.ends_with(".whl") || name.ends_with(".tmp") || name.ends_with(".py")) {
                    std::cout << YELLOW << "  - Removing " << name << RESET << std::endl;
                    fs::remove(entry.path());
                }
            }
        }
    }

    fs::path scripts_dir = cfg.spip_root / "scripts";
    if (fs::exists(scripts_dir)) {
        std::set<std::string> recognized_scripts = {
            "safe_extract.py", "audit_helper.py", "review_helper.py", 
            "verify_helper.py", "trim_helper.py", "agent_helper.py"
        };
        for (const auto& entry : fs::directory_iterator(scripts_dir)) {
            std::string name = entry.path().filename().string();
            if (recognized_scripts.find(name) == recognized_scripts.end()) {
                std::cout << YELLOW << "  - Removing unrecognized script: " << name << RESET << std::endl;
                fs::remove_all(entry.path());
            }
        }
    }

    // 3. Compact repositories (only if > 24h since last run)
    fs::path last_gc_file = cfg.spip_root / ".last_gc";
    bool run_gc = true;
    if (fs::exists(last_gc_file)) {
        auto last_gc_time = fs::last_write_time(last_gc_file);
        auto now = std::chrono::file_clock::now();
        if (now - last_gc_time < std::chrono::hours(24)) {
            run_gc = false;
        }
    }

    if (run_gc) {
        // 3. Compact Git Repo
        std::cout << MAGENTA << "📦 Compacting main repository (git gc)..." << RESET << std::endl;
        std::string gc_cmd = std::format("cd {} && git gc --prune=now --aggressive", quote_arg(cfg.repo_path.string()));
        std::system(gc_cmd.c_str());

        // 4. Compact DB Repo
        fs::path db_path = cfg.spip_root / "db";
        if (fs::exists(db_path)) {
            std::cout << MAGENTA << "📦 Compacting database repository (git gc)..." << RESET << std::endl;
            std::string db_gc_cmd = std::format("cd {} && git gc --prune=now --aggressive", quote_arg(db_path.string()));
            std::system(db_gc_cmd.c_str());
        }
        
        // Update last GC time
        std::ofstream ofs(last_gc_file);
        ofs << "Last GC run: " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << std::endl;
    } else {
        std::cout << BLUE << "ℹ️ Skipping git gc (last run within 24 hours)." << RESET << std::endl;
    }

    std::cout << GREEN << "✨ Cleanup complete." << RESET << std::endl;
    show_usage_stats(cfg);
}

void profile_package(const Config& cfg, const std::string& pkg, bool ai_review = false) {
    std::cout << MAGENTA << "📊 Profiling bytecode for package: " << BOLD << pkg << RESET << std::endl;
    
    // Find package in site-packages
    fs::path sp = get_site_packages(cfg);
    if (sp.empty()) {
        std::cerr << RED << "❌ No environment found. Run 'spip install <package>' first." << RESET << std::endl;
        return;
    }
    
    std::string pkg_lower = pkg;
    std::transform(pkg_lower.begin(), pkg_lower.end(), pkg_lower.begin(), ::tolower);
    std::string pkg_normalized = pkg_lower;
    std::replace(pkg_normalized.begin(), pkg_normalized.end(), '-', '_');
    
    fs::path pkg_path = sp / pkg_normalized;
    if (!fs::exists(pkg_path)) {
        pkg_path = sp / pkg_lower;
    }
    
    if (!fs::exists(pkg_path)) {
        std::cerr << RED << "❌ Package '" << pkg << "' not found in environment." << RESET << std::endl;
        std::cout << YELLOW << "💡 Available packages in site-packages:" << RESET << std::endl;
        int count = 0;
        for (const auto& entry : fs::directory_iterator(sp)) {
            if (entry.is_directory() && count++ < 20) {
                std::cout << "  - " << entry.path().filename().string() << std::endl;
            }
        }
        return;
    }
    
    // Run profiler script
    fs::path profiler = cfg.spip_root / "scripts" / "pyc_profiler.py";
    if (!fs::exists(profiler)) {
        std::cerr << RED << "❌ Profiler script not found: " << profiler << RESET << std::endl;
        return;
    }
    
    std::string cmd = std::format("python3 {} {}", quote_arg(profiler.string()), quote_arg(pkg_path.string()));
    std::string output = get_exec_output(cmd);
    
    // Parse JSON output
    if (output.find("{") == std::string::npos) {
        std::cerr << RED << "❌ Profiler failed: " << output << RESET << std::endl;
        return;
    }
    
    // Extract key metrics from JSON
    auto extract_number = [&](const std::string& key) -> long {
        size_t pos = output.find("\"" + key + "\":");
        if (pos == std::string::npos) return 0;
        size_t start = output.find_first_of("0123456789", pos);
        if (start == std::string::npos) return 0;
        size_t end = output.find_first_not_of("0123456789", start);
        try {
            return std::stol(output.substr(start, end - start));
        } catch (...) {
            return 0;
        }
    };
    
    long files = extract_number("files");
    long total_disk = extract_number("total_disk");
    long total_memory = extract_number("total_memory");
    long total_instructions = extract_number("total_instructions");
    long total_loops = extract_number("total_loops");
    long total_branches = extract_number("total_branches");
    long total_calls = extract_number("total_calls");
    
    // Display results
    std::cout << "\n" << BOLD << BLUE << "📦 Package: " << pkg << RESET << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    std::cout << std::format("{:<30} {:>15}", "Total .pyc files:", files) << std::endl;
    std::cout << std::format("{:<30} {:>12} KB", "Total disk usage:", total_disk / 1024) << std::endl;
    std::cout << std::format("{:<30} {:>12} KB", "Estimated memory footprint:", total_memory / 1024) << std::endl;
    
    std::cout << "\n" << BOLD << "Bytecode Complexity Metrics:" << RESET << std::endl;
    std::cout << std::format("{:<30} {:>15}", "Total instructions:", total_instructions) << std::endl;
    std::cout << std::format("{:<30} {:>15}", "Loop constructs:", total_loops) << std::endl;
    std::cout << std::format("{:<30} {:>15}", "Branch points:", total_branches) << std::endl;
    std::cout << std::format("{:<30} {:>15}", "Function calls:", total_calls) << std::endl;
    
    if (total_instructions > 0) {
        double complexity_factor = (double)(total_loops + total_branches) / total_instructions * 100;
        std::cout << std::format("{:<30} {:>14.2f}%", "Complexity factor:", complexity_factor) << std::endl;
    }

    // Static Vulnerabilities Analysis
    std::cout << "\n" << BOLD << MAGENTA << "🏛️ Static Function Analysis (Singletons/Caching):" << RESET << std::endl;
    auto extract_vuln = [&](const std::string& key) -> long {
        size_t pos = output.find("\"" + key + "\":");
        if (pos == std::string::npos) return 0;
        size_t start = output.find_first_of("0123456789", pos + key.length() + 3);
        size_t end = output.find_first_not_of("0123456789", start);
        return std::stol(output.substr(start, end - start));
    };

    long v1 = extract_vuln("method1_closure_free");
    long v2 = extract_vuln("method2_repeated_make");
    long v3 = extract_vuln("method3_const_calls");
    long v4 = extract_vuln("method4_purity_checks");

    std::cout << std::format("  {:<40} {:>10}", "Method 1: Closure-free nested defs:", v1) << std::endl;
    std::cout << std::format("  {:<40} {:>10}", "Method 2: Redundant MAKE_FUNCTION:", v2) << std::endl;
    std::cout << std::format("  {:<40} {:>10}", "Method 3: Constant argument calls:", v3) << std::endl;
    std::cout << std::format("  {:<40} {:>10}", "Method 4: Potential pure singletons:", v4) << std::endl;
    
    // Show top 32 most complex files
    std::cout << "\n" << BOLD << YELLOW << "🔥 Resource Hotspots (Top 32):" << RESET << std::endl;
    size_t detail_start = output.find("\"files_detail\":");
    if (detail_start != std::string::npos) {
        // Simple regex-free extraction of file details
        size_t arr_start = output.find("[", detail_start);
        size_t arr_end = output.find("]", arr_start);
        if (arr_start != std::string::npos && arr_end != std::string::npos) {
            std::string details = output.substr(arr_start + 1, arr_end - arr_start - 1);
            
            // Extract up to 32 file entries
            int count = 0;
            size_t pos = 0;
            while (count < 32 && (pos = details.find("\"path\":", pos)) != std::string::npos) {
                size_t obj_start = details.find_last_of("{", pos);
                size_t obj_end = details.find("}", pos);
                if (obj_end == std::string::npos) break;
                
                std::string obj = details.substr(obj_start, obj_end - obj_start + 1);
                
                // Extract path
                size_t p_key = obj.find("\"path\"");
                size_t path_start = obj.find("\"", p_key + 6);
                while (path_start != std::string::npos && obj[path_start] != '\"') path_start++;
                path_start++; // skip quote
                size_t path_end = obj.find("\"", path_start);
                std::string path = obj.substr(path_start, path_end - path_start);
                
                // Extract instructions
                size_t instr_key = obj.find("\"instructions\"");
                size_t instr_start = obj.find_first_of("0123456789", instr_key);
                size_t instr_end = obj.find_first_not_of("0123456789", instr_start);
                long instructions = std::stol(obj.substr(instr_start, instr_end - instr_start));
                
                // Show only filename
                fs::path p(path);
                std::cout << std::format("  {:<5} {:<50} {:>10} inst", count + 1, p.filename().string(), instructions) << std::endl;
                
                pos = obj_end + 1;
                count++;
            }
        }
    }

    // Show top redundant patterns
    std::cout << "\n" << BOLD << CYAN << "🔄 Redundant Constant Patterns (Top 10):" << RESET << std::endl;
    size_t pat_start = output.find("\"redundant_patterns\":");
    if (pat_start != std::string::npos) {
        size_t obj_start = output.find("{", pat_start);
        size_t obj_end = output.find("}", obj_start);
        if (obj_start != std::string::npos && obj_end != std::string::npos) {
            std::string patterns = output.substr(obj_start + 1, obj_end - obj_start - 1);
            int count = 0;
            size_t pos = 0;
            while (count < 10 && (pos = patterns.find("\"", pos)) != std::string::npos) {
                size_t key_end = patterns.find("\"", pos + 1);
                if (key_end == std::string::npos) break;
                std::string pattern = patterns.substr(pos + 1, key_end - pos - 1);
                
                size_t val_start = patterns.find(":", key_end) + 1;
                while (val_start < patterns.size() && (patterns[val_start] == ' ' || patterns[val_start] == '\n')) val_start++;
                size_t val_end = patterns.find_first_of(",\n}", val_start);
                std::string val_str = patterns.substr(val_start, val_end - val_start);
                
                std::cout << std::format("  {:<60} {:>8} occurrences", pattern, val_str) << std::endl;
                
                pos = patterns.find(",", val_start);
                if (pos == std::string::npos) break;
                count++;
            }
        }
    }
    
    if (ai_review) {
        const char* api_key = std::getenv("GEMINI_API_KEY");
        if (!api_key) {
            std::cout << YELLOW << "\n⚠️ GEMINI_API_KEY not set. Skipping AI review." << RESET << std::endl;
        } else {
            std::cout << CYAN << "\n🤖 Requesting AI Resource Optimization Review..." << RESET << std::endl;
            fs::path tmp_stats = fs::temp_directory_path() / "spip_profile_stats.json";
            std::ofstream ofs(tmp_stats);
            ofs << output;
            ofs.close();
            
            fs::path reviewer = cfg.spip_root / "scripts" / "profile_ai_review.py";
            std::string ai_cmd = std::format("python3 {} {} {} {}", 
                quote_arg(reviewer.string()), quote_arg(api_key), quote_arg(pkg), quote_arg(tmp_stats.string()));
            std::system(ai_cmd.c_str());
            fs::remove(tmp_stats);
        }
    }
    
    std::cout << std::endl;
}

void run_command(Config& cfg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: spip <install|uninstall|use|run|shell|list|cleanup|gc|log|search|tree|trim|verify|test|freeze|prune|audit|review|fetch-db|top|implement|boot|bundle|matrix|compat|profile> [args...]" << std::endl;
        std::cout << "  cleanup|gc [--all] - Perform maintenance, optionally remove all environments" << std::endl;
        std::cout << "  matrix <pkg> [--python version] [--profile] [--no-cleanup] [test.py] - Build-server mode: test all versions of a package" << std::endl;
        std::cout << "  compat <pkg> [N] [--profile] - Test compatibility against N latest Python versions" << std::endl;
        std::cout << "  profile <pkg> - Profile bytecode complexity, memory, and disk usage for an installed package" << std::endl;
        return;
    }
    std::string command = args[0];
    if (command == "bundle") {
        if (!require_args(args, 2, "Usage: spip bundle <folder>")) return;
        bundle_package(cfg, args[1]);
    }
    else if (command == "boot") {
        if (!require_args(args, 2, "Usage: spip boot <script.py>")) return;
        setup_project_env(cfg);
        boot_environment(cfg, args[1]);
    }
    else if (command == "fetch-db") {
        init_db();
        benchmark_mirrors(cfg);
        std::ifstream file("all_packages.txt");
        if (!file.is_open()) {
            std::cout << RED << "❌ all_packages.txt not found." << RESET << std::endl;
            return;
        }
        std::queue<std::string> q; std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                // Only fetch if metadata doesn't exist or is outdated
                if (!fs::exists(get_db_path(line)) || fs::last_write_time(get_db_path(line)) < fs::last_write_time("all_packages.txt")) {
                    q.push(line);
                }
            }
        }
        int total = q.size();
        if (total == 0) {
            std::cout << GREEN << "✨ All package metadata is up to date." << RESET << std::endl;
            return;
        }
        file.close();
        std::cout << MAGENTA << "📥 Fetching metadata for " << total << " packages (16 threads)..." << RESET << std::endl;
        std::mutex m; std::atomic<int> count{0}; const int num_threads = 16;
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) threads.emplace_back(db_worker, std::ref(q), std::ref(m), std::ref(count), total, cfg);
        for (auto& t : threads) t.join();
        std::cout << std::endl << GREEN << "✔ Fetch complete. Committing to Git..." << RESET << std::endl;
        std::string git_cmd = std::format("cd {} && git add packages && git commit -m \"Update package database\"", 
            quote_arg(cfg.repo_path.parent_path().string() + "/db"));
        std::system(git_cmd.c_str());
    }
    else if (command == "top") {
        std::string sort_order = "downloads"; // Default
        bool show_references = false; // Corresponds to --references
        bool show_dependencies = false; // Corresponds to --dependencies

        if (args.size() > 1) {
            for (size_t i = 1; i < args.size(); ++i) {
                if (args[i] == "--references") {
                    if (show_dependencies) { 
                        std::cerr << "Error: Cannot use --references with --dependencies." << std::endl;
                        return; 
                    }
                    show_references = true;
                } else if (args[i] == "--dependencies") {
                    if (show_references) { 
                        std::cerr << "Error: Cannot use --dependencies with --references." << std::endl;
                        return; 
                    }
                    show_dependencies = true;
                }
                // Add more flags here if needed
            }
        }
        show_top_packages(show_references, show_dependencies);
    }
    else if (command == "install" || command == "i") {
        setup_project_env(cfg);
        std::vector<std::string> targets; std::string pkg_str = "";
        for (size_t i = 1; i < args.size(); ++i) { 
            targets.push_back(args[i]); 
            pkg_str += args[i] + " "; 
            record_manual_install(cfg, args[i], true);
        }
        if (targets.empty()) { std::cout << "Usage: spip install <packages>" << std::endl; return; }
        resolve_and_install(cfg, targets);
        commit_state(cfg, "Manually installed " + pkg_str);
        std::cout << GREEN << "✔ Environment updated and committed." << RESET << std::endl;
        verify_environment(cfg);
    }
    else if (command == "uninstall" || command == "remove") {
        setup_project_env(cfg);
        if (args.size() < 2) { std::cout << "Usage: spip uninstall <packages>" << std::endl; return; }
        std::string pkg_str = "";
        for (size_t i = 1; i < args.size(); ++i) { 
            uninstall_package(cfg, args[i]); 
            pkg_str += args[i] + " "; 
            record_manual_install(cfg, args[i], false);
        }
        commit_state(cfg, "Uninstalled " + pkg_str);
        std::cout << GREEN << "✔ Uninstall committed to Git." << RESET << std::endl;
    }
    else if (command == "prune") {
        exec_with_setup(cfg, prune_orphans);
    }
    else if (command == "audit") {
        exec_with_setup(cfg, audit_environment);
    }
    else if (command == "review") {
        exec_with_setup(cfg, review_code);
    }
    else if (command == "implement") {
        std::string name, desc, ollama_model;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--name" && i + 1 < args.size()) name = args[++i];
            else if (args[i] == "--desc" && i + 1 < args.size()) desc = args[++i];
            else if (args[i] == "--ollama") {
                if (i + 1 < args.size() && args[i+1].find("--") != 0) ollama_model = args[++i];
                else ollama_model = "llama3"; // Default if no model specified
            }
        }
        if (name.empty() || desc.empty()) {
             std::cout << "Usage: spip implement --name <pkg> --desc \"<description>\" [--ollama [model]]" << std::endl;
             return;
        }
        setup_project_env(cfg);
        
        fs::path agent_path = cfg.spip_root / "scripts" / "agent_helper.py";

        fs::path python_bin = cfg.project_env_path / "bin" / "python";
        std::string run_cmd = std::format("{} {} {} {} {}", 
            quote_arg(python_bin.string()), quote_arg(agent_path.string()), 
            quote_arg(name), quote_arg(desc), quote_arg(ollama_model));
        
        std::system(run_cmd.c_str());
    }
    else if (command == "use") {
        if (args.size() < 2) { std::cerr << "Usage: spip use <version>" << std::endl; return; }
        std::string version = args[1];
        if (fs::exists(cfg.project_env_path)) {
            std::string rm_cmd = std::format("cd {} && git worktree remove {} --force", 
                quote_arg(cfg.repo_path.string()), quote_arg(cfg.project_env_path.string()));
            std::system(rm_cmd.c_str());
            std::string del_branch = std::format("cd {} && git branch -D project/{}", 
                quote_arg(cfg.repo_path.string()), cfg.project_hash);
            std::system(del_branch.c_str());
        }
        setup_project_env(cfg, version);
        std::cout << GREEN << "✔ Project now using Python " << version << RESET << std::endl;
    }
    else if (command == "log") {
        setup_project_env(cfg);
        std::string cmd = std::format("cd {} && git log --oneline --graph", quote_arg(cfg.project_env_path.string()));
        std::system(cmd.c_str());
    }
    else if (command == "run") {
        setup_project_env(cfg);
        fs::path bin_path = cfg.project_env_path / "bin";
        std::string path_env = std::format("PATH={}:{}", quote_arg(bin_path.string()), quote_arg(std::getenv("PATH")));
        std::string cmd = "";
        for (size_t i = 1; i < args.size(); ++i) cmd += quote_arg(args[i]) + " ";
        std::string full_cmd = path_env + " " + cmd;
        std::system(full_cmd.c_str());
    }
    else if (command == "shell") {
        setup_project_env(cfg);
        fs::path bin_path = cfg.project_env_path / "bin";
        std::string shell = std::getenv("SHELL") ? std::getenv("SHELL") : "/bin/bash";
        std::string env_vars = std::format("VIRTUAL_ENV={} PATH={}:{}", 
            quote_arg(cfg.project_env_path.string()), quote_arg(bin_path.string()), quote_arg(std::getenv("PATH")));
        std::string full_cmd = env_vars + " " + shell;
        std::system(full_cmd.c_str());
    }
    else if (command == "search") {
        if (args.size() < 2) {
            std::cout << "Usage: spip search <query>" << std::endl;
            return;
        }
        std::string query = args[1];
        std::transform(query.begin(), query.end(), query.begin(), ::tolower);
        
        std::ifstream file("all_packages.txt");
        if (!file.is_open()) {
            std::cout << RED << "❌ all_packages.txt not found. Run a crawler or fetch-db first." << RESET << std::endl;
            return;
        }

        std::cout << MAGENTA << "🔍 Searching for '" << query << "'..." << RESET << std::endl;
        std::string line;
        int matches = 0;
        while (std::getline(file, line)) {
            std::string lower_line = line;
            std::transform(lower_line.begin(), lower_line.end(), lower_line.begin(), ::tolower);
            if (lower_line.find(query) != std::string::npos) {
                matches++;
                std::cout << GREEN << "📦 " << BOLD << line << RESET;
                
                // Try to get description if metadata exists locally
                fs::path metadata_path = get_db_path(line);
                if (fs::exists(metadata_path)) {
                    std::ifstream ifs(metadata_path);
                    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
                    std::string desc = extract_field(content, "summary");
                    if (!desc.empty()) std::cout << " - " << desc;
                }
                std::cout << std::endl;
                
                if (matches >= 50) {
                    std::cout << YELLOW << "... and more. Narrow your search." << RESET << std::endl;
                    break;
                }
            }
        }
        if (matches == 0) std::cout << "No matches found." << std::endl;
    }
    else if (command == "tree") {
        if (args.size() < 2) {
            std::cout << "Usage: spip tree <package>" << std::endl;
            return;
        }
        std::set<std::string> visited;
        print_tree(args[1], 0, visited);
    }
    else if (command == "trim") {
        if (args.size() < 2) {
            std::cout << "Usage: spip trim <script.py>" << std::endl;
            return;
        }
        setup_project_env(cfg);
        trim_environment(cfg, args[1]);
    }
    else if (command == "verify") {
        setup_project_env(cfg);
        verify_environment(cfg);
    }
    else if (command == "test") {
        if (args.size() < 2) {
            std::cout << "Usage: spip test <package|--all>" << std::endl;
            return;
        }
        setup_project_env(cfg);
        if (args[1] == "--all") {
            run_all_package_tests(cfg);
        } else {
            run_package_tests(cfg, args[1]);
        }
    }
    else if (command == "freeze" || command == "--freeze") {
        if (args.size() < 2) {
            std::cout << "Usage: spip freeze <filename.tgz>" << std::endl;
            return;
        }
        setup_project_env(cfg);
        freeze_environment(cfg, args[1]);
    }
    else if (command == "matrix") {
        ensure_dirs(cfg);
        if (args.size() < 2) {
             std::cerr << "Usage: spip matrix <package> [options]" << std::endl;
             return;
        }

        std::string pkg = "";
        std::string test_script = "";
        std::string python_ver = "auto";
        bool profile = false;
        bool telemetry = false;
        bool no_cleanup = false;
        int revision_limit = -1;
        bool test_all_revisions = false;
        bool smoke_test = false;

        // Skip 'matrix' (0)
        for (size_t i = 1; i < args.size(); ++i) {
            std::string arg = args[i];
            if (arg == "--python" && i + 1 < args.size()) {
                python_ver = args[++i];
            } else if (arg == "--profile") {
                profile = true;
            } else if (arg == "--telemetry") {
                telemetry = true;
            } else if (arg == "--smoke") {
                smoke_test = true;
            } else if (arg == "--no-cleanup") {
                no_cleanup = true;
            } else if (arg == "--limit" && i + 1 < args.size()) {
                revision_limit = std::stoi(args[++i]);
            } else if (arg == "--all") {
                test_all_revisions = true;
            } else if (arg.starts_with("--")) {
                std::cerr << "Unknown option: " << arg << std::endl;
            } else {
                if (pkg.empty()) pkg = arg;
                else if (test_script.empty()) test_script = arg;
            }
        }

        if (pkg.empty()) {
            std::cerr << "Error: Package name required." << std::endl;
            return;
        }
        
        Config m_cfg = cfg;
        m_cfg.telemetry = telemetry;
        // Default to hardware_concurrency if not overridden
        for (size_t i = 1; i < args.size(); ++i) {
             if ((args[i] == "--threads" || args[i] == "-j") && i + 1 < args.size()) {
                 m_cfg.concurrency = std::stoi(args[++i]);
             }
        }
        if (smoke_test) run_thread_test(m_cfg);
        matrix_test(m_cfg, pkg, test_script, python_ver, profile, no_cleanup, revision_limit, test_all_revisions, false);
    }
    else if (command == "compat") {
        ensure_dirs(cfg);
        if (args.size() < 2) {
             std::cerr << "Usage: spip compat <package> [options]" << std::endl;
             std::cerr << "Options:" << std::endl;
             std::cerr << "  [N]                   Top N Python versions (default 3)" << std::endl;
             std::cerr << "  --py <N>              Top N Python versions" << std::endl;
             std::cerr << "  --pkg <M>             Top M Package versions" << std::endl;
             std::cerr << "  --profile             Enable resource profiling" << std::endl;
             return;
        }

        std::string pkg = args[1];
        int n_py = 3; 
        int m_pkg = 1;
        bool profile = false;
        bool telemetry = false;
        bool smoke_test = false;
        
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "--profile") profile = true;
            else if (args[i] == "--telemetry") telemetry = true;
            else if (args[i] == "--smoke") smoke_test = true;
            else if (args[i] == "--py" && i + 1 < args.size()) n_py = std::stoi(args[++i]);
            else if (args[i] == "--pkg" && i + 1 < args.size()) m_pkg = std::stoi(args[++i]);
            else {
                try {
                    // Legacy positional arg check
                    if (args[i].find("-") != 0) n_py = std::stoi(args[i]);
                } catch (...) {}
            }
        }
        
        Config m_cfg = cfg;
        m_cfg.telemetry = telemetry;
        for (size_t i = 2; i < args.size(); ++i) {
             if ((args[i] == "--threads" || args[i] == "-j") && i + 1 < args.size()) {
                 m_cfg.concurrency = std::stoi(args[++i]);
             }
        }
        if (smoke_test) run_thread_test(m_cfg);
        matrix_test(m_cfg, pkg, "", "auto", profile, false, n_py, false, true, m_pkg);
    }
    else if (command == "profile") {
        if (!require_args(args, 2, "Usage: spip profile <pkg> [--ai|--review]")) return;
        bool ai_review = false;
        std::string pkg = "";
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--ai" || args[i] == "--review") ai_review = true;
            else if (pkg.empty()) pkg = args[i];
        }
        if (pkg.empty()) {
            std::cerr << "Error: Package name required." << std::endl;
            return;
        }
        setup_project_env(cfg);
        profile_package(cfg, pkg, ai_review);
    }
    else if (command == "bench") {
        int threads = cfg.concurrency;
        bool telemetry = false;
        bool network = false;
        for (size_t i = 1; i < args.size(); ++i) {
            if ((args[i] == "--threads" || args[i] == "-j") && i + 1 < args.size()) {
                threads = std::stoi(args[++i]);
            } else if (args[i] == "--telemetry") {
                telemetry = true;
            } else if (args[i] == "--network") {
                network = true;
            }
        }
        Config b_cfg = cfg;
        b_cfg.concurrency = threads;
        b_cfg.telemetry = telemetry;
        
        if (network) {
            benchmark_mirrors(b_cfg);
            benchmark_concurrency(b_cfg);
        } else {
            run_thread_test(b_cfg);
        }
    }
    else if (command == "list") {
        ensure_dirs(cfg);
        show_usage_stats(cfg);
        std::cout << BOLD << "Managed Environment Branches:" << RESET << std::endl;
        std::string cmd = std::format("cd \"{}\" && git branch", cfg.repo_path.string());
        std::system(cmd.c_str());
    }
    else if (command == "cleanup" || command == "gc") {
        bool remove_all = (args.size() > 1 && args[1] == "--all");
        cleanup_spip(cfg, remove_all);
    }
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    Config cfg = init_config();
    run_command(cfg, args);
    return 0;
}

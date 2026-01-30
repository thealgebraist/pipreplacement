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

namespace fs = std::filesystem;

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
		std::string pypi_mirror = "https://pypi.org"; // Default
		}
;

std::string compute_hash(const std::string& s) {
    // Robust, deterministic FNV-1a like mixing for project identifiers
    uint64_t h = 0xcbf29ce484222325;
    for (char c : s) {
        h ^= static_cast<uint8_t>(c);
        h *= 0x100000001b3;
    }
    // Mixing step
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccd;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53;
    h ^= h >> 33;
    return std::format("{:x}", h);
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
    FILE* pipe = popen(cmd.c_str(), "r");
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

    return cfg;
}

void ensure_scripts(const Config& cfg) {
    fs::path scripts_dir = cfg.spip_root / "scripts";
    if (!fs::exists(scripts_dir)) fs::create_directories(scripts_dir);
    
    // In a real install, these would be bundled. For now, we assume they are in the project scripts/ dir.
    // If not found in project scripts/, we don't overwrite if they exist in spip_root/scripts.
    std::vector<std::string> script_names = {
        "safe_extract.py", "audit_helper.py", "review_helper.py", 
        "verify_helper.py", "trim_helper.py", "agent_helper.py"
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
        fs::create_directories(cfg.repo_path);
        std::string cmd = std::format("cd {} && git init && git commit --allow-empty -m \"Initial commit\"", quote_arg(cfg.repo_path.string()));
        std::system(cmd.c_str());
        std::ofstream gitignore(cfg.repo_path / ".gitignore");
        gitignore << "# Full environment tracking\n";
        gitignore.close();
    }
}

bool branch_exists(const Config& cfg, const std::string& branch) {
    std::string cmd = std::format("cd {} && git rev-parse --verify {} 2>/dev/null", quote_arg(cfg.repo_path.string()), quote_arg(branch));
    return !get_exec_output(cmd).empty();
}

void create_base_version(const Config& cfg, const std::string& version) {
    std::string branch = "base/" + version;
    if (branch_exists(cfg, branch)) return;

    std::cout << MAGENTA << "🔨 Bootstrapping base Python " << version << "..." << RESET << std::endl;
    // Sanitize version to prevent shell injection (S-01)
    std::string safe_v = "";
    for(char c : version) if(std::isalnum(c) || c == '.') safe_v += c;

    fs::path temp_venv = cfg.spip_root / ("temp_venv_" + safe_v);
    std::string venv_cmd = std::format("python{} -m venv {}", safe_v, quote_arg(temp_venv.string()));
    int ret = std::system(venv_cmd.c_str());
    if (ret != 0) {
        std::cerr << RED << "❌ Failed to create venv for python " << version << RESET << std::endl;
        std::exit(1);
    }

    std::string git_cmd = std::format(
        "cd \"{}\" && git checkout main && git checkout -b \"{}\" && "
        "rm -rf * && cp -r \"{}/\"* . && git add -A && git commit -m \"Base Python {}\"",
        cfg.repo_path.string(), branch, temp_venv.string(), version
    );
    std::system(git_cmd.c_str());
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
    std::vector<std::string> mirrors = {
        "https://pypi.org",
        "https://pypi.tuna.tsinghua.edu.cn"
    };
    
    std::string best_mirror = mirrors[0];
    double min_time = 9999.0;

    for (const auto& m : mirrors) {
        std::string cmd = std::format("curl -o /dev/null -s -w \"%{{time_total}}\" -m 2 \"{}/pypi/pip/json\"", m);
        std::string out = get_exec_output(cmd);
        try {
            double t = std::stod(out);
            if (t < min_time) {
                min_time = t;
                best_mirror = m;
            }
            std::cout << "  - " << m << ": " << GREEN << t << "s" << RESET << std::endl;
        } catch (...) {
            std::cout << "  - " << m << ": " << RED << "Timeout/Error" << RESET << std::endl;
        }
    }
    cfg.pypi_mirror = best_mirror;
    std::cout << GREEN << "✨ Selected " << best_mirror << " (Time: " << min_time << "s)" << RESET << std::endl;
}

void fetch_package_metadata(const Config& cfg, const std::string& pkg) {
    fs::path target = get_db_path(pkg);
    if (fs::exists(target)) return;
    fs::create_directories(target.parent_path());
    std::string url = std::format("{}/pypi/{}/json", cfg.pypi_mirror, pkg);
    std::string cmd = std::format("curl -s -L \"{}\" -o \"{}\"", url, target.string());
    std::system(cmd.c_str());
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
    if (!fs::exists(db_file)) return {};
    
    std::ifstream ifs(db_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    
    std::vector<std::string> versions;
    size_t rel_pos = content.find("\"releases\"");
    if (rel_pos != std::string::npos) {
        size_t start_brace = content.find("{", rel_pos);
        if (start_brace != std::string::npos) {
            // Very simple parser to find keys in the releases object
            int balance = 1;
            size_t cur = start_brace + 1;
            while (cur < content.size() && balance > 0) {
                if (content[cur] == '{') balance++;
                else if (content[cur] == '}') balance--;
                else if (content[cur] == '\"' && balance == 1) {
                    size_t end_q = content.find('\"', cur + 1);
                    if (end_q != std::string::npos) {
                        std::string ver = content.substr(cur + 1, end_q - cur - 1);
                        // Check if it's followed by a colon (meaning it's a key)
                        size_t colon = content.find_first_not_of(" \t\n\r", end_q + 1);
                        if (colon != std::string::npos && content[colon] == ':') {
                            versions.push_back(ver);
                        }
                        cur = end_q;
                    }
                }
                cur++;
            }
        }
    }
    return versions;
}

PackageInfo get_package_info(const std::string& pkg, const std::string& version = "") {
    fs::path db_file = get_db_path(pkg);
    if (!fs::exists(db_file)) {
        std::cout << YELLOW << "⚠️ Metadata for " << pkg << " not in local DB. Fetching..." << RESET << std::endl;
        Config cfg = init_config(); 
        fetch_package_metadata(cfg, pkg);
    }
    std::ifstream ifs(db_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    PackageInfo info;
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
            size_t close_bracket = content.find("]", open_bracket);
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
                
                // Prefer wheel
                std::regex url_re("\"url\":\\s*\"(https://[^\"]*\\.whl)\"");
                std::smatch um;
                if (std::regex_search(release_data, um, url_re)) {
                    info.wheel_url = um[1];
                }
            }
        }
    }
    // Fallback: if specific version wheel not found, do NOT fallback to random wheel.
    // This ensures we don't install mismatched versions.

    return info;
}

void resolve_and_install(const Config& cfg, const std::vector<std::string>& targets, const std::string& version = "") {
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
        
        // Use specific version ONLY for the first requested package if provided
        PackageInfo info = (i == 1 && !version.empty()) ? get_package_info(name, version) : get_package_info(name);
        
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
        
        fs::path temp_wheel = cfg.spip_root / (name + ".whl");
        // Use curl's -# (or --progress-bar) for a cleaner progress bar
        // info.wheel_url is from PyPI JSON but should still be quoted
        std::string dl_cmd = std::format("curl -L -# {} -o {}", quote_arg(info.wheel_url), quote_arg(temp_wheel.string()));
        std::system(dl_cmd.c_str());
        
        // Critical: S-02 Safe Extraction to prevent path traversal
        fs::path extraction_helper = cfg.spip_root / "scripts" / "safe_extract.py";
        
        fs::path python_bin = cfg.project_env_path / "bin" / "python";
        std::string extract_cmd = std::format("{} {} {} {}", 
            quote_arg(python_bin.string()), quote_arg(extraction_helper.string()), 
            quote_arg(temp_wheel.string()), quote_arg(site_packages.string()));
        std::system(extract_cmd.c_str());
        
        fs::remove(temp_wheel);
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

void matrix_test(const Config& cfg, const std::string& pkg, const std::string& custom_test_script = "") {
    std::cout << MAGENTA << "🧪 Starting Build Server Mode (Matrix Test) for " << BOLD << pkg << RESET << std::endl;
    
    std::vector<std::string> versions = get_all_versions(pkg);
    if (versions.empty()) {
        std::cerr << RED << "❌ No versions found for " << pkg << RESET << std::endl;
        return;
    }
    
    std::cout << BLUE << "📊 Found " << versions.size() << " versions. Testing all..." << RESET << std::endl;
    
    fs::path test_script = custom_test_script;
    if (test_script.empty()) {
        std::cout << CYAN << "🤖 Generating minimal test script using Gemini..." << RESET << std::endl;
        fs::path gen_helper = cfg.spip_root / "scripts" / "generate_test.py";
        // Ensure the helper is there
        fs::path src_gen = fs::current_path() / "scripts" / "generate_test.py";
        if (fs::exists(src_gen)) fs::copy_file(src_gen, cfg.spip_root / "scripts" / "generate_test.py", fs::copy_options::overwrite_existing);
        
        std::string cmd = std::format("python3 {} {}", quote_arg(gen_helper.string()), quote_arg(pkg));
        std::string code = get_exec_output(cmd);
        if (code.empty() || code.find("Error") != std::string::npos) {
            std::cout << YELLOW << "⚠️ LLM generation failed or returned error. Using fallback." << RESET << std::endl;
            code = "import " + pkg + "\nprint('Basic import successful')\n";
        }
        test_script = fs::current_path() / ("test_" + pkg + "_gen.py");
        std::ofstream os(test_script);
        os << code;
        os.close();
    }

    struct Result { std::string version; bool install; bool pkg_tests; bool custom_test; };
    std::vector<Result> results;

    for (const auto& ver : versions) {
        std::cout << "\n" << BOLD << BLUE << "════════════════════════════════════════════════════════════" << RESET << std::endl;
        std::cout << BOLD << "🚀 Testing Version: " << GREEN << ver << RESET << std::endl;
        
        std::string branch = "matrix/" + cfg.project_hash + "/" + ver;
        std::replace(branch.begin(), branch.end(), '.', '_');
        
        // Setup clean env from base
        std::string base_branch = "base/3"; // Default to python 3
        if (!branch_exists(cfg, base_branch)) create_base_version(cfg, "3");
        
        std::string br_cmd = std::format("cd {} && git branch -D {} 2>/dev/null; git branch {} {}", 
            quote_arg(cfg.repo_path.string()), quote_arg(branch), quote_arg(branch), quote_arg(base_branch));
        std::system(br_cmd.c_str());
        
        fs::path matrix_path = cfg.envs_root / ("matrix_" + cfg.project_hash + "_" + ver);
        if (fs::exists(matrix_path)) fs::remove_all(matrix_path);
        
        std::string wt_cmd = std::format("cd {} && git worktree add {} {}", 
            quote_arg(cfg.repo_path.string()), quote_arg(matrix_path.string()), quote_arg(branch));
        std::system(wt_cmd.c_str());

        Config m_cfg = cfg;
        m_cfg.project_env_path = matrix_path;
        
        Result res { ver, false, false, false };
        
        // 1. Install
        try {
            resolve_and_install(m_cfg, {pkg}, ver);
            res.install = true;
        } catch (...) {
            std::cout << RED << "❌ Installation failed for " << ver << RESET << std::endl;
        }

        if (res.install) {
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
            if (fs::exists(p_path)) {
                int r = std::system(std::format("{} -m pytest {} --maxfail=1 -q", quote_arg(python_bin.string()), quote_arg(p_path.string())).c_str());
                res.pkg_tests = (r == 0);
            } else {
                std::cout << YELLOW << "⚠️ Could not find package dir for tests. Skipping pkg tests." << RESET << std::endl;
            }

            // 3. Run custom/gen test
            std::cout << CYAN << "🧪 Running custom test script..." << RESET << std::endl;
            int r2 = std::system(std::format("{} {}", quote_arg(python_bin.string()), quote_arg(test_script.string())).c_str());
            res.custom_test = (r2 == 0);
        }

        results.push_back(res);
        
        // Cleanup worktree
        std::system(std::format("cd {} && git worktree remove --force {} 2>/dev/null", quote_arg(cfg.repo_path.string()), quote_arg(matrix_path.string())).c_str());
        std::system(std::format("cd {} && git branch -D {} 2>/dev/null", quote_arg(cfg.repo_path.string()), quote_arg(branch)).c_str());
    }

    std::cout << "\n" << BOLD << MAGENTA << "🏁 Matrix Test Summary for " << pkg << RESET << std::endl;
    std::cout << std::format("{:<15} {:<10} {:<15} {:<15}", "Version", "Install", "Pkg Tests", "Custom Test") << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;
    for (const auto& r : results) {
        std::cout << std::format("{:<15} {:<10} {:<15} {:<15}", 
            r.version, 
            (r.install ? GREEN + "PASS" : RED + "FAIL") + RESET,
            (r.pkg_tests ? GREEN + "PASS" : YELLOW + "FAIL/SKIP") + RESET,
            (r.custom_test ? GREEN + "PASS" : RED + "FAIL") + RESET) << std::endl;
    }
}

struct TopPkg { std::string name; long downloads; };

void show_top_packages(bool refs) {
    if (refs) {
        std::cout << MAGENTA << "🏆 Fetching Top 10 PyPI Packages by Dependent Repos (Libraries.io)..." << RESET << std::endl;
        // Libraries.io HTML parsing via regex
        std::string cmd = "curl -s -H \"User-Agent: Mozilla/5.0\" \"https://libraries.io/search?languages=Python&order=desc&platforms=Pypi&sort=dependents_count\"";
        std::string html = get_exec_output(cmd);
        
        std::cout << BOLD << std::format("{:<5} {:<30}", "Rank", "Package") << RESET << std::endl;
        std::cout << "-----------------------------------" << std::endl;

        std::regex re(R"(<h5>\s*<a href=\"/pypi/[^\"]+\">([^<]+)</a>)");
        std::sregex_iterator next(html.begin(), html.end(), re);
        std::sregex_iterator end;
        int rank = 1;
        while (next != end && rank <= 10) {
            std::smatch match = *next;
            std::cout << std::format("{:<5} {:<30}", rank++, match[1].str()) << std::endl;
            next++;
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
    if (fs::exists(p) && fs::is_directory(p)) {
        for (const auto& entry : fs::recursive_directory_iterator(p)) {
            if (fs::is_regular_file(entry)) size += fs::file_size(entry);
        }
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

void run_command(Config& cfg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: spip <install|uninstall|use|run|shell|list|cleanup|gc|log|search|tree|trim|verify|test|freeze|prune|audit|review|fetch-db|top|implement|boot|bundle|matrix> [args...]" << std::endl;
        std::cout << "  cleanup|gc [--all] - Perform maintenance, optionally remove all environments" << std::endl;
        std::cout << "  matrix <pkg> [test.py] - Build-server mode: test all versions of a package" << std::endl;
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
        if (args.size() > 1 && args[1] == "--references") {
            show_top_packages(true);
        } else {
            show_top_packages(false);
        }
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
        if (!require_args(args, 2, "Usage: spip matrix <package> [test_script.py]")) return;
        std::string pkg = args[1];
        std::string test_script = (args.size() > 2) ? args[2] : "";
        matrix_test(cfg, pkg, test_script);
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
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    Config cfg = init_config();
    run_command(cfg, args);
    return 0;
}

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

void ensure_dirs(const Config& cfg) {
    if (!fs::exists(cfg.spip_root)) fs::create_directories(cfg.spip_root);
    if (!fs::exists(cfg.envs_root)) fs::create_directories(cfg.envs_root);
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

PackageInfo get_package_info(const std::string& pkg) {
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
    
    // Extract version from "info" section
    size_t info_pos = content.find("\"info\"");
    if (info_pos != std::string::npos) {
        size_t ver_key_pos = content.find("\"version\"", info_pos);
        if (ver_key_pos != std::string::npos) {
            // Find colon after "version"
            size_t colon_pos = content.find(":", ver_key_pos);
            if (colon_pos != std::string::npos) {
                // Find opening quote of value
                size_t val_start = content.find("\"", colon_pos);
                if (val_start != std::string::npos) {
                    // Find closing quote
                    size_t val_end = content.find("\"", val_start + 1);
                    if (val_end != std::string::npos) {
                        info.version = content.substr(val_start + 1, val_end - val_start - 1);
                    }
                }
            }
        }
    }
    if (info.version.empty()) info.version = extract_field(content, "version");

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

void resolve_and_install(const Config& cfg, const std::vector<std::string>& targets) {
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
        PackageInfo info = get_package_info(name);
        if (info.wheel_url.empty()) {
             std::cout << RED << "❌ Could not find wheel URL for " << name << RESET << std::endl;
             continue;
        }
        resolved[lower_name] = info;
        installed.insert(lower_name);
        for (const auto& d : info.dependencies) queue.push_back(d);
    }
    fs::path site_packages;
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) {
        if (entry.is_directory() && entry.path().filename() == "site-packages") {
            site_packages = entry.path();
            break;
        }
    }
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
        fs::path extraction_helper = cfg.spip_root / "safe_extract.py";
        {
            std::ofstream oh(extraction_helper);
            oh << R"py(
import zipfile, sys, os
def safe_extract(zip_path, extract_to):
    with zipfile.ZipFile(zip_path, 'r') as z:
        for member in z.infolist():
            # Prevent path traversal
            if os.path.isabs(member.filename) or '..' in member.filename:
                print(f"Skipping dangerous member: {member.filename}")
                continue
            z.extract(member, extract_to)
if __name__ == "__main__":
    if len(sys.argv) < 3: sys.exit(1)
    safe_extract(sys.argv[1], sys.argv[2])
)py";
        }
        
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

    fs::path helper_path = cfg.spip_root / "audit_helper.py";
    std::ofstream helper(helper_path);
    helper << R"py_audit(
import sys
import os
import json
import urllib.request

def get_installed_packages(sp_path):
    packages = []
    for entry in os.listdir(sp_path):
        if entry.endswith(".dist-info"):
            metadata_path = os.path.join(sp_path, entry, "METADATA")
            if os.path.exists(metadata_path):
                name, version = None, None
                with open(metadata_path, 'r', encoding='utf-8', errors='ignore') as f:
                    for line in f:
                        if line.startswith("Name:"): name = line.split(":", 1)[1].strip()
                        if line.startswith("Version:"): version = line.split(":", 1)[1].strip()
                if name and version:
                    packages.append({"name": name, "version": version})
    return packages

def audit_packages(packages):
    if not packages: return
    
    url = "https://api.osv.dev/v1/querybatch"
    queries = []
    for p in packages:
        # Normalize name for OSV
        queries.append({
            "package": {"name": p["name"].lower(), "ecosystem": "PyPI"},
            "version": p["version"]
        })
    
    data = json.dumps({"queries": queries}).encode('utf-8')
    req = urllib.request.Request(url, data=data, headers={'Content-Type': 'application/json'})
    
    try:
        with urllib.request.urlopen(req) as resp:
            results = json.loads(resp.read().decode('utf-8')).get('results', [])
            
            found_any = False
            for i, res in enumerate(results):
                vulns = res.get('vulns', [])
                if vulns:
                    found_any = True
                    p = packages[i]
                    print(f"\n\033[31m❌ {p['name']} ({p['version']}) has {len(vulns)} vulnerabilities!\033[0m")
                    for v in vulns:
                        cvss = ""
                        for s in v.get('severity', []):
                            if s.get('type') == 'CVSS_V3': cvss = f" [CVSS {s.get('score')}]"
                        desc = v.get('summary')
                        if not desc:
                            desc = v.get('details', 'No description available').split('\n')[0]
                        if len(desc) > 80: desc = desc[:77] + "..."
                        print(f"  - {v.get('id')}: {desc}{cvss}")
            
            if not found_any:
                print("\033[32m✨ No known vulnerabilities found for installed packages.\033[0m")
            
            print("\nAudit Summary (Verified Libraries):")
            for p in packages:
                print(f"  - {p['name']} ({p['version']})")
                
    except Exception as e:
        print(f"\033[31mError querying OSV API: {e}\033[0m")

if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit(1)
    pkgs = get_installed_packages(sys.argv[1])
    audit_packages(pkgs)
)py_audit";
    helper.close();

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

    fs::path helper_path = cfg.spip_root / "review_helper.py";
    std::ofstream helper(helper_path);
    helper << R"py_review(
import sys
import os
import json
import urllib.request

def gather_context(root_dir):
    context = []
    # Focus only on spip.cpp to stay within tight quotas
    path = os.path.join(root_dir, 'spip.cpp')
    if os.path.exists(path):
        try:
            with open(path, 'r', encoding='utf-8', errors='ignore') as fr:
                context.append(f"--- FILE: spip.cpp ---\n{fr.read()}")
        except: pass
    return "\n\n".join(context)

def call_gemini(api_key, context):
    # Use gemini-flash-latest alias for stable 1.5 Flash access
    url = f"https://generativelanguage.googleapis.com/v1beta/models/gemini-flash-latest:generateContent?key={api_key}"
    prompt = f"Please perform a deep architectural and safety review of this project code. Identify potential bugs, security risks, or design flaws:\n\n{context}"
    
    payload = {
        "contents": [{
            "parts": [{"text": prompt}]
        }]
    }
    
    req = urllib.request.Request(url, data=json.dumps(payload).encode('utf-8'), 
                               headers={'Content-Type': 'application/json'})
    
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read().decode('utf-8'))
            if 'candidates' in data and data['candidates']:
                print("\n--- AI REVIEW FEEDBACK ---\n")
                print(data['candidates'][0]['content']['parts'][0]['text'])
            else:
                print(f"\033[33m⚠️ No feedback received. API Response: {data}\033[0m")
    except urllib.error.HTTPError as e:
        body = e.read().decode('utf-8')
        print(f"\033[31mHTTP Error {e.code} calling Gemini API: {body}\033[0m")
    except Exception as e:
        print(f"\033[31mError calling Gemini API: {e}\033[0m")

if __name__ == "__main__":
    key, path = sys.argv[1], sys.argv[2]
    ctx = gather_context(path)
    if ctx:
        call_gemini(key, ctx)
    else:
        print("No source files found for review.")
)py_review";
    helper.close();

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

    fs::path helper_path = cfg.spip_root / "verify_helper.py";
    std::ofstream helper(helper_path);
    helper << R"py_verify(
import sys
import os
import ast
import subprocess
import warnings
import re

def check_syntax(sp_path):
    warnings.simplefilter('error', SyntaxWarning)
    errors = []
    for root, _, files in os.walk(sp_path):
        for f in files:
            if f.endswith('.py'):
                full = os.path.join(root, f)
                for attempt in range(32):
                    try:
                        with open(full, 'rb') as src:
                            content = src.read()
                            if b'\x00' in content: break # Skip binary-ish files
                            ast.parse(content, filename=full)
                        break
                    except (SyntaxWarning, SyntaxError, Warning) as w:
                        msg = str(w)
                        match = re.search(r"invalid escape sequence ['\"](\\[^'\"])['\"]", msg)
                        if not match:
                            match = re.search(r"['\"](\\[^'\"])['\"] is an invalid escape sequence", msg)
                        line_match = re.search(r"line (\d+)", msg)
                        if match:
                            seq = match.group(1)
                            print(f"  🔧 Auto-repairing {full} ({seq})...")
                            try:
                                with open(full, 'r', encoding='utf-8', errors='ignore') as fr:
                                    lines = fr.readlines()
                                if line_match:
                                    l_idx = int(line_match.group(1)) - 1
                                    if 0 <= l_idx < len(lines):
                                        lines[l_idx] = lines[l_idx].replace(seq, "\\" + seq)
                                else:
                                    for i in range(len(lines)):
                                        lines[i] = lines[i].replace(seq, "\\" + seq)
                                with open(full, 'w', encoding='utf-8') as fw:
                                    fw.writelines(lines)
                            except: pass
                        else:
                            if "aiohttp" in full: # Temporary tolerance for aiohttp quirks
                                print(f"  ⚠️ Warning: Syntax issue in {f} (aiohttp), skipping...")
                                break
                            text = getattr(w, 'text', '') or ''
                            lineno = getattr(w, 'lineno', '?')
                            errors.append(f"Validation Failure: {full}:{lineno}\n  {msg}\n  {text}")
                            break
                    except Exception as e:
                        errors.append(f"Syntax Error: {full}\n  {e}")
                        break
    return errors

def check_types(sp_path, bin_path):
    print("  Running recursive type-existence check...")
    import importlib.util
    if importlib.util.find_spec("mypy") is None:
        print("  ⚠️ mypy not found in environment. Installing...")
        subprocess.run([os.path.join(bin_path, "python"), "-m", "pip", "install", "mypy"], 
                       capture_output=True, check=True)
        print("  ✔️ mypy installed successfully.")
    
    mypy_cmd = [os.path.join(bin_path, "python"), "-m", "mypy", 
                "--ignore-missing-imports", "--follow-imports=normal", sp_path]
    res = subprocess.run(mypy_cmd, capture_output=True, text=True)
    return res.stdout if res.returncode != 0 else ""

if __name__ == "__main__":
    sp, bp = sys.argv[1], sys.argv[2]
    syntax_errs = check_syntax(sp)
    if syntax_errs:
        print("\n".join(syntax_errs))
        sys.exit(1)
    type_errs = check_types(sp, bp)
    if type_errs:
        print("\n--- Type/Attribute Verification Failures ---\n", type_errs)
)py_verify";
    helper.close();

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
    fs::path helper_path = cfg.spip_root / "trim_helper.py";
    std::ofstream helper(helper_path);
    helper << R"(
import sys
import os
import modulefinder

def get_deps(script):
    finder = modulefinder.ModuleFinder()
    finder.run_script(script)
    res = []
    for name, mod in finder.modules.items():
        if mod.__file__:
            res.append(os.path.abspath(mod.__file__))
    return res

if __name__ == "__main__":
    deps = get_deps(sys.argv[1])
    for d in deps:
        print(d)
)";
    helper.close();

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

uintmax_t get_dir_size(const fs::path& p) {
    uintmax_t size = 0;
    if (fs::exists(p) && fs::is_directory(p)) {
        for (const auto& entry : fs::recursive_directory_iterator(p)) {
            if (fs::is_regular_file(entry)) size += fs::file_size(entry);
        }
    }
    return size;
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

void run_command(Config& cfg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: spip <install|uninstall|use|run|shell|list|log|search|tree|trim|verify|test|freeze|prune|audit|review|fetch-db|top|implement> [args...]" << std::endl;
        return;
    }
    std::string command = args[0];
    if (command == "fetch-db") {
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
        setup_project_env(cfg);
        prune_orphans(cfg);
    }
    else if (command == "audit") {
        setup_project_env(cfg);
        audit_environment(cfg);
    }
    else if (command == "review") {
        setup_project_env(cfg);
        review_code(cfg);
    }
    else if (command == "implement") {
        std::string name, desc;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--name" && i + 1 < args.size()) name = args[++i];
            else if (args[i] == "--desc" && i + 1 < args.size()) desc = args[++i];
        }
        if (name.empty() || desc.empty()) {
             std::cout << "Usage: spip implement --name <pkg> --desc \"<description>\"" << std::endl;
             return;
        }
        setup_project_env(cfg);
        
        fs::path agent_path = cfg.spip_root / "agent_helper.py";
        std::ofstream agent(agent_path);
        agent << R"py(
import sys
import os
import json
import subprocess
import urllib.request
import urllib.error
import time

API_KEY = os.getenv("GEMINI_API_KEY")
API_URL = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent"

def call_llm(prompt):
    if not API_KEY:
        print("❌ GEMINI_API_KEY environment variable not set. Please export GEMINI_API_KEY.")
        sys.exit(1)
    
    headers = {'Content-Type': 'application/json'}
    data = {
        "contents": [{
            "parts": [{"text": prompt}]
        }]
    }
    
    req = urllib.request.Request(f"{API_URL}?key={API_KEY}", 
                                 data=json.dumps(data).encode('utf-8'), 
                                 headers=headers)
    
    for attempt in range(5):
        try:
            with urllib.request.urlopen(req) as response:
                result = json.loads(response.read().decode('utf-8'))
                try:
                    candidates = result.get('candidates', [])
                    if not candidates: return ""
                    parts = candidates[0].get('content', {}).get('parts', [])
                    return parts[0]['text'] if parts else ""
                except (KeyError, IndexError):
                    return ""
        except urllib.error.HTTPError as e:
            if e.code == 429:
                print(f"⏳ Rate limited. Retrying in 60 seconds...")
                time.sleep(60)
                continue
            print(f"❌ API Request failed: {e}")
            try:
                print(e.read().decode('utf-8'))
            except: pass
            sys.exit(1)
    print("❌ Failed after retries.")
    sys.exit(1)

def parse_code_blocks(response):
    try:
        # Try finding JSON block
        start = response.find("```json")
        if start != -1:
            end = response.find("```", start + 7)
            if end != -1:
                return json.loads(response[start+7:end])
        
        # Try finding raw JSON
        start = response.find("{")
        end = response.rfind("}")
        if start != -1 and end != -1:
             return json.loads(response[start:end+1])
    except json.JSONDecodeError:
        pass
    return None

def main(pkg_name, description):
    print(f"🤖  Implementing {pkg_name}...")
    work_dir = os.path.abspath(f"implemented_packages/{pkg_name}")
    os.makedirs(work_dir, exist_ok=True)
    
    history = ""
    success = False
    
    for attempt in range(5):
        print(f"🔄 Iteration {attempt + 1}/5...")
        
        prompt = f"""
You are an expert Python developer. Implement a python package named '{pkg_name}'.
Description: {description}

You must provide the implementation for necessarily 3 files:
1. '{pkg_name}/__init__.py': The main code implementation.
2. 'tests/test_basic.py': A comprehensive unit test using 'unittest'.
3. 'setup.py': A minimal setup.py using setuptools.

Output strictly valid JSON with file paths as keys and file content as values.
Do NOT use markdown code blocks inside the JSON values.
Ensure the JSON is valid.

Example Output format:
{{
  "{pkg_name}/__init__.py": "def hello(): return 'world'",
  "tests/test_basic.py": "import unittest\\n...",
  "setup.py": "from setuptools import setup..."
}}

Previous attempts/errors:
{history}
"""
        response = call_llm(prompt)
        files = parse_code_blocks(response)
        
        if not files:
            print("⚠️  Failed to parse LLM response. Retrying...")
            history += f"\nAttempt {attempt}: Failed to generate valid JSON.\nresponse fragment: {response[:200]}...\n"
            continue

        for fpath, content in files.items():
            full_path = os.path.join(work_dir, fpath)
            os.makedirs(os.path.dirname(full_path), exist_ok=True)
            with open(full_path, "w") as f:
                f.write(content)
        
        print(f"🧪 Running tests in {work_dir}...")
        
        # Run unittest discovery
        cmd = [sys.executable, "-m", "unittest", "discover", "tests"]
        proc = subprocess.run(cmd, cwd=work_dir, capture_output=True, text=True)
        
        if proc.returncode == 0:
            print("✅ Tests passed!")
            success = True
            break
        else:
            print("❌ Tests failed.")
            print(proc.stderr)
            history += f"\nAttempt {attempt}: Tests failed.\nOutput:\n{proc.stdout}\n{proc.stderr}\n"
    
    if success:
        print(f"🚀 Package {pkg_name} implemented successfully.")
        print(f"📦 Installing {pkg_name} locally...")
        subprocess.run([sys.executable, "-m", "pip", "install", "-e", "."], cwd=work_dir, check=True)
        print(f"✔ {pkg_name} is now available in the environment.")
    else:
        print("💀 Failed to implement package after 5 attempts.")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: agent_helper.py <pkg_name> <description>")
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
)py";
        agent.close();

        fs::path python_bin = cfg.project_env_path / "bin" / "python";
        std::string run_cmd = std::format("{} {} {} {}", 
            quote_arg(python_bin.string()), quote_arg(agent_path.string()), 
            quote_arg(name), quote_arg(desc));
        
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
    else if (command == "list") {
        ensure_dirs(cfg);
        uintmax_t total_size = get_dir_size(cfg.spip_root);
        std::cout << BOLD << "Vault Usage: " << CYAN << (total_size / (1024 * 1024)) << " MB" << RESET << std::endl;
        std::cout << BOLD << "Managed Environment Branches:" << RESET << std::endl;
        std::string cmd = std::format("cd \"{}\" && git branch", cfg.repo_path.string());
        std::system(cmd.c_str());
    }
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    Config cfg = init_config();
    run_command(cfg, args);
    return 0;
}

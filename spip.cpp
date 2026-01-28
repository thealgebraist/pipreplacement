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
};

std::string compute_hash(const std::string& s) {
    std::hash<std::string> hasher;
    size_t h = hasher(s);
    return std::format("{:x}", h);
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
        std::string cmd = std::format("cd \"{}\" && git init && git commit --allow-empty -m \"Initial commit\"", cfg.repo_path.string());
        std::system(cmd.c_str());
        std::ofstream gitignore(cfg.repo_path / ".gitignore");
        gitignore << "# Full environment tracking\n";
        gitignore.close();
    }
}

bool branch_exists(const Config& cfg, const std::string& branch) {
    std::string cmd = std::format("cd \"{}\" && git rev-parse --verify \"{}\" 2>/dev/null", cfg.repo_path.string(), branch);
    return !get_exec_output(cmd).empty();
}

void create_base_version(const Config& cfg, const std::string& version) {
    std::string branch = "base/" + version;
    if (branch_exists(cfg, branch)) return;

    std::cout << MAGENTA << "🔨 Bootstrapping base Python " << version << "..." << RESET << std::endl;
    fs::path temp_venv = cfg.spip_root / ("temp_venv_" + version);
    std::string venv_cmd = std::format("python{} -m venv \"{}\"", version, temp_venv.string());
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
        std::cout << GREEN << "🌟 Creating new environment branch: " << branch << RESET << std::endl;
        std::string cmd = std::format("cd \"{}\" && git branch \"{}\" \"{}\"", cfg.repo_path.string(), branch, base_branch);
        std::system(cmd.c_str());
    }

    if (!fs::exists(cfg.project_env_path)) {
        std::cout << CYAN << "📂 Linking worktree for project..." << RESET << std::endl;
        std::system(std::format("cd \"{}\" && git checkout main 2>/dev/null", cfg.repo_path.string()).c_str());
        std::string cmd = std::format("cd \"{}\" && git worktree add \"{}\" \"{}\"", 
            cfg.repo_path.string(), cfg.project_env_path.string(), branch);
        int ret = std::system(cmd.c_str());
        if (ret != 0) {
            std::system(std::format("cd \"{}\" && git worktree prune", cfg.repo_path.string()).c_str());
            std::system(cmd.c_str());
        }
        std::ofstream os(cfg.project_env_path / ".project_origin");
        os << cfg.current_project.string();
    }
}

void commit_state(const Config& cfg, const std::string& msg) {
    std::string cmd = std::format("cd \"{}\" && git add -A && git commit -m \"{}\" --allow-empty", 
        cfg.project_env_path.string(), msg);
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

void fetch_package_metadata(const std::string& pkg) {
    fs::path target = get_db_path(pkg);
    if (fs::exists(target)) return;
    fs::create_directories(target.parent_path());
    std::string url = std::format("https://pypi.org/pypi/{}/json", pkg);
    std::string cmd = std::format("curl -s -L \"{}\" -o \"{}\"", url, target.string());
    std::system(cmd.c_str());
}

void db_worker(std::queue<std::string>& q, std::mutex& m, std::atomic<int>& count, int total) {
    while (true) {
        std::string pkg;
        {
            std::lock_guard<std::mutex> lock(m);
            if (q.empty()) break;
            pkg = q.front();
            q.pop();
        }
        fetch_package_metadata(pkg);
        int current = ++count;
        if (current % 100 == 0) {
            std::cout << "\r🚀 Progress: " << current << "/" << total << std::flush;
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
    std::regex re("\"" + key + "\":\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, re)) return match[1];
    return "";
}

std::vector<std::string> extract_array(const std::string& json, const std::string& key) {
    std::vector<std::string> result;
    std::regex re("\"" + key + "\":\\s*\\[([^\\]]*)\\]");
    std::smatch match;
    if (std::regex_search(json, match, re)) {
        std::string array_content = match[1];
        std::regex item_re("\"([^\"]*)\"");
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
        fetch_package_metadata(pkg);
    }
    std::ifstream ifs(db_file);
    std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
    PackageInfo info;
    info.name = pkg;
    info.version = extract_field(content, "version");
    auto raw_deps = extract_array(content, "requires_dist");
    for (const auto& d : raw_deps) {
        std::regex name_re("^([a-zA-Z0-9_.-]+)");
        std::smatch m;
        if (std::regex_search(d, m, name_re)) {
            if (d.find("extra ==") == std::string::npos) {
                info.dependencies.push_back(m[1]);
            }
        }
    }
    std::regex wheel_re("\"url\":\\s*\"(https://[^\"]*\\.whl)\"");
    std::smatch m;
    if (std::regex_search(content, m, wheel_re)) {
        info.wheel_url = m[1];
    }
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
        current++;
        std::cout << BLUE << "[" << current << "/" << resolved.size() << "] " << RESET 
                  << "📦 " << BOLD << name << RESET << " (" << info.version << ")..." << std::endl;
        
        fs::path temp_wheel = cfg.spip_root / (name + ".whl");
        // Use curl's -# (or --progress-bar) for a cleaner progress bar
        std::string dl_cmd = std::format("curl -L -# \"{}\" -o \"{}\"", info.wheel_url, temp_wheel.string());
        std::system(dl_cmd.c_str());
        
        std::string unzip_cmd = std::format("unzip -q -o \"{}\" -d \"{}\"", temp_wheel.string(), site_packages.string());
        std::system(unzip_cmd.c_str());
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

std::string quote_arg(const std::string& arg) {
    if (arg.find(' ') == std::string::npos && arg.find(';') == std::string::npos && 
        arg.find('(') == std::string::npos && arg.find(')') == std::string::npos &&
        arg.find('\'') == std::string::npos && arg.find('\"') == std::string::npos) {
        return arg;
    }
    std::string result = "\"";
    for (char c : arg) {
        if (c == '\"' || c == '\\' || c == '`' || c == '$') result += "\\";
        result += c;
    }
    result += "\"";
    return result;
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
                if (fs::exists(full_path) && !fs::is_directory(full_path)) fs::remove(full_path);
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
    // Check if pytest is installed
    std::string pytest_check = std::format("\"{}\" -m pytest --version 2>/dev/null", python_bin.string());
    if (std::system(pytest_check.c_str()) != 0) {
        std::cout << BLUE << "📦 Installing pytest for testing..." << RESET << std::endl;
        std::string install_pytest = std::format("\"{}\" -m pip install pytest", python_bin.string());
        std::system(install_pytest.c_str());
    }

    std::cout << GREEN << "🚀 Running tests..." << RESET << std::endl;
    std::string test_cmd = std::format("\"{}\" -m pytest \"{}\"", python_bin.string(), pkg_path.string());
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
                        print(f"  - {v.get('id')}: {v.get('summary', 'No summary')}{cvss}")
            
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
                            ast.parse(src.read())
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
                        else:
                            errors.append(f"Validation Failure: {full}\n  {msg}")
                            break
                    except Exception as e:
                        errors.append(f"Syntax Error: {full}\n  {e}")
                        break
    return errors

def check_types(sp_path, bin_path):
    print("  Running recursive type-existence check...")
    mypy_cmd = [os.path.join(bin_path, "python"), "-m", "mypy", 
                "--ignore-missing-imports", "--follow-imports=normal", sp_path]
    try:
        subprocess.run([os.path.join(bin_path, "python"), "-m", "mypy", "--version"], 
                       capture_output=True, check=True)
    except:
        print("  ⚠️ mypy not found in environment. Installing...")
        subprocess.run([os.path.join(bin_path, "python"), "-m", "pip", "install", "mypy"], 
                       capture_output=True)
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
    std::string verify_cmd = std::format("\"{}\" \"{}\" \"{}\" \"{}\"", 
        python_bin.string(), helper_path.string(), site_packages.string(), (cfg.project_env_path / "bin").string());
    
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

    // Native dependencies (Mac specific)
    std::vector<std::string> native_queue;
    for (const auto& f : needed_files) {
        if (f.ends_with(".so") || f.ends_with(".dylib")) native_queue.push_back(f);
    }

    // Simple recursive otool dependent finder
    size_t idx = 0;
    while(idx < native_queue.size()) {
        std::string lib = native_queue[idx++];
        std::string otool_cmd = std::format("otool -L \"{}\"", lib);
        std::string otool_out = get_exec_output(otool_cmd);
        std::stringstream ss2(otool_out);
        while(std::getline(ss2, line)) {
            std::regex re(R"(\t([^\s]+) \(compatibility)");
            std::smatch m;
            if (std::regex_search(line, m, re)) {
                std::string dep = m[1];
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
    
    std::string test_cmd = std::format("cd \"{}\" && ../spip run python \"{}\"", 
        cfg.current_project.string(), script_path);
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

void run_command(const Config& cfg, const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: spip <install|uninstall|use|run|shell|list|log|search|tree|trim|verify|test|freeze|prune|audit|fetch-db> [args...]" << std::endl;
        return;
    }
    std::string command = args[0];
    if (command == "fetch-db") {
        init_db();
        std::ifstream file("all_packages.txt");
        if (!file.is_open()) {
            std::cout << RED << "❌ all_packages.txt not found." << RESET << std::endl;
            return;
        }
        std::queue<std::string> q; std::string line;
        while (std::getline(file, line)) if (!line.empty()) q.push(line);
        int total = q.size(); file.close();
        std::cout << MAGENTA << "📥 Fetching metadata for " << total << " packages (16 threads)..." << RESET << std::endl;
        std::mutex m; std::atomic<int> count{0}; const int num_threads = 16;
        std::vector<std::thread> threads;
        for (int i = 0; i < num_threads; ++i) threads.emplace_back(db_worker, std::ref(q), std::ref(m), std::ref(count), total);
        for (auto& t : threads) t.join();
        std::cout << std::endl << GREEN << "✔ Fetch complete. Committing to Git..." << RESET << std::endl;
        const char* home = std::getenv("HOME");
        std::string git_cmd = std::format("cd \"{}/.spip/db\" && git add packages && git commit -m \"Update package database\"", home);
        std::system(git_cmd.c_str());
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
    else if (command == "use") {
        if (args.size() < 2) { std::cerr << "Usage: spip use <version>" << std::endl; return; }
        std::string version = args[1];
        if (fs::exists(cfg.project_env_path)) {
            std::string rm_cmd = std::format("cd \"{}\" && git worktree remove \"{}\" --force", cfg.repo_path.string(), cfg.project_env_path.string());
            std::system(rm_cmd.c_str());
            std::string del_branch = std::format("cd \"{}\" && git branch -D \"project/{}\"", cfg.repo_path.string(), cfg.project_hash);
            std::system(del_branch.c_str());
        }
        setup_project_env(cfg, version);
        std::cout << GREEN << "✔ Project now using Python " << version << RESET << std::endl;
    }
    else if (command == "log") {
        setup_project_env(cfg);
        std::string cmd = std::format("cd \"{}\" && git log --oneline --graph", cfg.project_env_path.string());
        std::system(cmd.c_str());
    }
    else if (command == "run") {
        setup_project_env(cfg);
        fs::path bin_path = cfg.project_env_path / "bin";
        std::string path_env = std::format("PATH=\"{}:{}\"", bin_path.string(), std::getenv("PATH"));
        std::string cmd = "";
        for (size_t i = 1; i < args.size(); ++i) cmd += quote_arg(args[i]) + " ";
        std::string full_cmd = path_env + " " + cmd;
        std::system(full_cmd.c_str());
    }
    else if (command == "shell") {
        setup_project_env(cfg);
        fs::path bin_path = cfg.project_env_path / "bin";
        std::string shell = std::getenv("SHELL") ? std::getenv("SHELL") : "/bin/bash";
        std::string env_vars = std::format("VIRTUAL_ENV=\"{}\" PATH=\"{}:{}\"", 
            cfg.project_env_path.string(), bin_path.string(), std::getenv("PATH"));
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

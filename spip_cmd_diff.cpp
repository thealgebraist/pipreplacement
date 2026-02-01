#include "spip_cmd.h"
#include "spip_diff.h"
#include "spip_utils.h"
#include <iostream>
#include <cstdlib>
#include <regex>

static std::string fetch_wheel_url(const std::string& package, const std::string& version) {
    std::string json_file = "/tmp/pypi_" + package + "_" + version + ".json";
    std::string url = "https://pypi.org/pypi/" + package + "/" + version + "/json";
    std::string cmd = "curl -sSL " + quote_arg(url) + " > " + quote_arg(json_file);
    
    if (system(cmd.c_str()) != 0) return "";
    
    // Read JSON
    std::ifstream f(json_file);
    if (!f.is_open()) return "";
    
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    unlink(json_file.c_str());
    
    // Find universal wheel URL
    std::regex url_re(R"(\"url\":\s*\"(https://[^\"]*py3-none-any\.whl)\")");
    std::smatch match;
    if (std::regex_search(content, match, url_re)) {
        return match[1].str();
    }
    
    // Try any wheel
    url_re = std::regex(R"(\"url\":\s*\"(https://[^\"]*\.whl)\")");
    if (std::regex_search(content, match, url_re)) {
        return match[1].str();
    }
    
    return "";
}

int cmd_diff(int argc, char** argv) {
    if (argc < 1) {
        std::cerr << "Usage: spip diff <package> [--limit N]\n";
        return 1;
    }
    
    std::string package = argv[0];
    int limit = 10;
    
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--limit" && i + 1 < argc) {
            limit = std::atoi(argv[i + 1]);
            i++;
        }
    }
    
    std::cout << "🔍 Fetching versions for " << package << "...\n";
    auto versions = fetch_versions(package, limit);
    
    if (versions.empty()) {
        std::cerr << "❌ No versions found\n";
        return 1;
    }
    
    std::cout << "📥 Found " << versions.size() << " versions\n";
    
    // Download wheels
    std::cout << "⬇️  Downloading wheels...\n";
    for (auto& v : versions) {
        auto wheel_url = fetch_wheel_url(package, v.version);
        if (wheel_url.empty()) {
            std::cerr << "  ✗ " << v.version << " (no wheel)\n";
            continue;
        }
        
        std::string filename = wheel_url.substr(wheel_url.rfind('/') + 1);
        std::string local_path = "/tmp/" + filename;
        
        std::string cmd = "curl -sSL -o " + quote_arg(local_path) + " " + quote_arg(wheel_url);
        if (system(cmd.c_str()) == 0) {
            v.wheel_path = local_path;
            
            // Get file size
            FILE* f = fopen(local_path.c_str(), "rb");
            if (f) {
                fseek(f, 0, SEEK_END);
                v.wheel_size = ftell(f);
                fclose(f);
            }
            
            std::cout << "  ✓ " << v.version << " (" << (v.wheel_size / 1024) << " KB)\n";
        } else {
            std::cerr << "  ✗ " << v.version << " (download failed)\n";
        }
    }
    
    // Remove versions without wheels
    versions.erase(std::remove_if(versions.begin(), versions.end(),
        [](const VersionDiff& v) { return v.wheel_path.empty(); }), versions.end());
    
    if (versions.size() < 2) {
        std::cerr << "❌ Need at least 2 versions with wheels\n";
        return 1;
    }
    
    // Compute diffs
    std::cout << "\n🔬 Computing VCDIFF deltas...\n";
    std::vector<DiffResult> results;
    
    for (size_t i = 0; i < versions.size(); i++) {
        for (size_t j = i + 1; j < std::min(versions.size(), i + 4); j++) {
            auto dr = compute_vcdiff(versions[i].wheel_path, versions[j].wheel_path);
            dr.version_a = versions[i].wheel_path;
            dr.version_b = versions[j].wheel_path;
            results.push_back(dr);
            
            std::cout << "  " << versions[i].version << " ↔ " << versions[j].version 
                      << ": delta=" << (dr.delta_size / 1024) << " KB, similarity=" 
                      << (int)(dr.similarity * 100) << "%\n";
        }
    }
    
    print_diff_matrix(versions, results);
    
    // Cleanup
    for (const auto& v : versions) {
        unlink(v.wheel_path.c_str());
    }
    
    return 0;
}

#include "spip_cmd.h"
#include "spip_diff.h"
#include "spip_delta_db.h"
#include "spip_utils.h"
#include <iostream>
#include <cstdlib>
#include <regex>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

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
        std::cerr << "Usage: spip diff <package> [--limit N] [--store]\n";
        return 1;
    }
    
    std::string package = argv[0];
    int limit = 10;
    bool store_deltas = false;
    
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--limit" && i + 1 < argc) {
            limit = std::atoi(argv[i + 1]);
            i++;
        } else if (std::string(argv[i]) == "--store") {
            store_deltas = true;
        }
    }
    
    if (store_deltas) {
        init_delta_db();
        std::cout << "💾 Delta storage enabled\n";
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
    std::vector<std::string> wheel_urls;
    for (auto& v : versions) {
        auto wheel_url = fetch_wheel_url(package, v.version);
        wheel_urls.push_back(wheel_url);
        
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
    
    std::string delta_cache = (fs::path(getenv("HOME")) / ".spip" / "delta_cache").string();
    fs::create_directories(delta_cache);
    
    for (size_t i = 0; i < versions.size(); i++) {
        for (size_t j = i + 1; j < std::min(versions.size(), i + 4); j++) {
            auto dr = compute_vcdiff(versions[i].wheel_path, versions[j].wheel_path);
            dr.version_a = versions[i].wheel_path;
            dr.version_b = versions[j].wheel_path;
            results.push_back(dr);
            
            std::cout << "  " << versions[i].version << " → " << versions[j].version 
                      << ": delta=" << (dr.delta_size / 1024) << " KB, similarity=" 
                      << (int)(dr.similarity * 100) << "%";
            
            // Show savings
            if (dr.delta_size < versions[j].wheel_size) {
                int savings = 100 - (dr.delta_size * 100 / versions[j].wheel_size);
                std::cout << " (" << savings << "% savings)";
            }
            std::cout << "\n";
            
            // Store delta if requested
            if (store_deltas && dr.delta_size < versions[j].wheel_size * 0.7) {
                // Copy delta to permanent storage
                std::string delta_filename = package + "_" + versions[i].version + 
                                            "_to_" + versions[j].version + ".vcdiff";
                std::string perm_delta = delta_cache + "/" + delta_filename;
                
                // Re-create delta in permanent location
                std::string cmd = "xdelta3 -e -s " + quote_arg(versions[i].wheel_path) + " " +
                                 quote_arg(versions[j].wheel_path) + " " + 
                                 quote_arg(perm_delta) + " 2>/dev/null";
                
                if (system(cmd.c_str()) == 0) {
                    DeltaRecord rec;
                    rec.package_name = package;
                    rec.source_version = versions[i].version;
                    rec.target_version = versions[j].version;
                    rec.delta_size = dr.delta_size;
                    rec.target_size = versions[j].wheel_size;
                    rec.similarity = dr.similarity;
                    rec.delta_path = perm_delta;
                    rec.source_url = wheel_urls[i];
                    rec.target_url = wheel_urls[j];
                    rec.created_at = std::time(nullptr);
                    
                    store_delta(rec);
                    std::cout << "    💾 Stored delta for future use\n";
                }
            }
        }
    }
    
    print_diff_matrix(versions, results);
    
    // Cleanup
    for (const auto& v : versions) {
        unlink(v.wheel_path.c_str());
    }
    
    return 0;
}

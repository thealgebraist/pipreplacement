#include "spip_diff.h"
#include "spip_db.h"
#include "spip_utils.h"
#include "spip_python.h"
#include <iostream>
#include <algorithm>
#include <cstdio>

std::vector<VersionDiff> fetch_versions(const std::string& package, int limit) {
    std::vector<VersionDiff> result;
    
    // Get all versions from DB
    auto versions = get_all_versions(package);
    if (versions.empty()) {
        std::cerr << "❌ Could not fetch versions for " << package << "\n";
        return result;
    }
    
    // Take last N versions (versions should already be sorted)
    int start = std::max(0, (int)versions.size() - limit);
    for (int i = start; i < (int)versions.size(); i++) {
        const auto& ver = versions[i];
        
        // Try to get wheel URL - for now just use package name as placeholder
        // We'll download directly in the command
        VersionDiff vd;
        vd.version = ver;
        vd.wheel_path = "";  // Will be filled in later
        vd.wheel_size = 0;
        result.push_back(vd);
    }
    
    return result;
}

DiffResult compute_vcdiff(const std::string& path_a, const std::string& path_b) {
    DiffResult dr;
    dr.version_a = path_a;
    dr.version_b = path_b;
    dr.delta_size = 0;
    dr.similarity = 0.0;
    
    // Create temp file for delta
    std::string delta_path = "/tmp/delta_" + std::to_string(getpid()) + ".vcdiff";
    
    // Run xdelta3 to create delta: xdelta3 -e -s source target delta
    std::string cmd = "xdelta3 -e -s " + quote_arg(path_a) + " " + 
                      quote_arg(path_b) + " " + quote_arg(delta_path) + " 2>/dev/null";
    
    int ret = system(cmd.c_str());
    if (ret == 0) {
        // Get delta size
        FILE* f = fopen(delta_path.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            dr.delta_size = ftell(f);
            fclose(f);
        }
        
        // Get target size
        FILE* ft = fopen(path_b.c_str(), "rb");
        size_t target_size = 0;
        if (ft) {
            fseek(ft, 0, SEEK_END);
            target_size = ftell(ft);
            fclose(ft);
        }
        
        // Similarity: 1 - (delta_size / target_size)
        if (target_size > 0) {
            dr.similarity = 1.0 - ((double)dr.delta_size / target_size);
            if (dr.similarity < 0) dr.similarity = 0;
        }
        
        unlink(delta_path.c_str());
    }
    
    return dr;
}

void print_diff_matrix(const std::vector<VersionDiff>& versions, const std::vector<DiffResult>& results) {
    std::cout << "\n📊 VCDIFF Binary Similarity Matrix\n\n";
    
    // Print header
    std::cout << "Version      ";
    for (size_t i = 0; i < std::min(versions.size(), size_t(5)); i++) {
        printf("%-10s ", versions[i].version.c_str());
    }
    std::cout << "\n";
    std::cout << std::string(70, '-') << "\n";
    
    // Print matrix
    for (size_t i = 0; i < versions.size(); i++) {
        printf("%-12s ", versions[i].version.c_str());
        
        for (size_t j = 0; j < std::min(versions.size(), size_t(5)); j++) {
            if (i == j) {
                std::cout << "100%       ";
            } else {
                // Find result
                bool found = false;
                for (const auto& r : results) {
                    if ((r.version_a == versions[i].wheel_path && r.version_b == versions[j].wheel_path) ||
                        (r.version_a == versions[j].wheel_path && r.version_b == versions[i].wheel_path)) {
                        printf("%.0f%%        ", r.similarity * 100);
                        found = true;
                        break;
                    }
                }
                if (!found) std::cout << "-          ";
            }
        }
        std::cout << "\n";
    }
}

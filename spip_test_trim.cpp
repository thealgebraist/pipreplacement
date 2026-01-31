#include "spip_test.h"
#include "spip_env.h"

void trim_environment(const Config& cfg, const std::string& script_path) {
    if (!fs::exists(script_path)) { std::cerr << RED << "❌ Script not found." << RESET << std::endl; return; }
    std::cout << MAGENTA << "✂️ Trimming environment..." << RESET << std::endl;
    std::string ts = std::format("{:x}", std::chrono::system_clock::now().time_since_epoch().count());
    std::string tb = "trim/" + cfg.project_hash + "/" + ts.substr(ts.length() - 6);
    run_shell(std::format("cd \"{}\" && git checkout -b \"{}\"", cfg.project_env_path.string(), tb).c_str());
    fs::path h = cfg.spip_root / "scripts" / "trim_helper.py"; fs::path py = cfg.project_env_path / "bin" / "python";
    std::string out = get_exec_output(std::format("\"{}\" \"{}\" \"{}\"", py.string(), h.string(), script_path));
    std::set<std::string> needed; std::stringstream ss(out); std::string line;
    while (std::getline(ss, line)) if (!line.empty()) needed.insert(fs::absolute(line).string());
    needed.insert((cfg.project_env_path / "pyvenv.cfg").string()); needed.insert((cfg.project_env_path / "bin" / "python").string());
    std::vector<std::string> nat; for (const auto& f : needed) if (f.ends_with(".so") || f.ends_with(".dylib")) nat.push_back(f);
    size_t idx = 0; while(idx < nat.size()) {
        std::string lib = nat[idx++]; std::string dout = get_exec_output(std::format("otool -L {} 2>/dev/null || ldd {} 2>/dev/null", quote_arg(lib), quote_arg(lib)));
        std::stringstream ss2(dout); while(std::getline(ss2, line)) {
            std::regex re(R"(\t([^\s]+) (compatibility|=>\s+([^\s]+)\s+\())"); std::smatch m;
            if (std::regex_search(line, m, re)) {
                std::string d = m[1].matched ? m[1].str() : m[2].str();
                if (d.find(cfg.project_env_path.string()) != std::string::npos && needed.find(d) == needed.end()) { needed.insert(d); nat.push_back(d); }
            }
        }
    }
    for (const auto& entry : fs::recursive_directory_iterator(cfg.project_env_path)) if (entry.is_regular_file()) {
        std::string p = fs::absolute(entry.path()).string(); if (needed.find(p) == needed.end() && p.find(".git") == std::string::npos) fs::remove(entry.path());
    }
    if (run_shell(std::format("cd {} && ../spip run python {}", quote_arg(cfg.current_project.string()), quote_arg(script_path)).c_str()) == 0) {
        std::cout << GREEN << "✨ Trim successful!" << RESET << std::endl; commit_state(cfg, "Trimmed environment");
    } else { std::cout << RED << "❌ Trim failed! Reverting..." << RESET << std::endl; run_shell(std::format("cd \"{}\" && git checkout -", cfg.project_env_path.string()).c_str()); }
}

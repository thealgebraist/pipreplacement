#include "spip_python.h"

std::string ensure_python_bin(const Config& cfg, const std::string& version) {
    std::string safe_v = ""; for(char c : version) if(std::isalnum(c) || c == '.') safe_v += c;
    std::string python_bin = "python" + safe_v;
    if (!get_exec_output(std::format("command -v {} 2>/dev/null", python_bin)).empty()) return python_bin; 
    if (safe_v == "2.7" && !get_exec_output("command -v python2 2>/dev/null").empty()) return "python2";
    fs::path pythons_dir = cfg.spip_root / "pythons";
    fs::path install_bin_dir = pythons_dir / safe_v / "python" / "bin";
    if (safe_v == "2.7") {
        if (fs::exists(install_bin_dir / "python")) return (install_bin_dir / "python").string();
        if (fs::exists(install_bin_dir / "python2")) return (install_bin_dir / "python2").string();
    } else if (fs::exists(install_bin_dir / "python3")) return (install_bin_dir / "python3").string();
    fs::path local_python = pythons_dir / safe_v / "bin" / ("python" + safe_v);
    if (fs::exists(local_python)) return local_python.string();
    std::cout << YELLOW << "⚠️  " << python_bin << " not found. Downloading standalone build..." << RESET << std::endl;
    if (!fs::exists(pythons_dir)) fs::create_directories(pythons_dir);
    std::string tag = "20241016"; std::string full_ver = get_full_version_map(safe_v); std::string platform = get_platform_tuple();
    std::string filename = std::format("cpython-{}+{}-{}-install_only.tar.gz", full_ver, tag, platform);
    std::string url = std::format("https://github.com/indygreg/python-build-standalone/releases/download/{}/{}", tag, filename);
    fs::path archive_path = pythons_dir / filename; fs::path dest_dir = pythons_dir / safe_v;
    std::cout << BLUE << "📥 Downloading " << url << "..." << RESET << std::endl;
    std::string dl_cmd = std::format("curl -L -s -# {} -o {}", quote_arg(url), quote_arg(archive_path.string()));
    if (run_shell(dl_cmd.c_str()) != 0 || !fs::exists(archive_path) || fs::file_size(archive_path) < 1000) {
        std::cerr << RED << "❌ Failed to download Python " << full_ver << " from " << url << RESET << std::endl; return "python3"; 
    }
    std::cout << BLUE << "📦 Unpacking to " << dest_dir.string() << "..." << RESET << std::endl;
    fs::create_directories(dest_dir);
    run_shell(std::format("tar -xzf {} -C {}", quote_arg(archive_path.string()), quote_arg(dest_dir.string())).c_str());
    fs::remove(archive_path);
    local_python = dest_dir / "python" / "bin" / (safe_v == "2.7" ? "python" : "python3");
    return fs::exists(local_python) ? local_python.string() : "python3";
}

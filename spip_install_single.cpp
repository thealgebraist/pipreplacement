#include "spip_install.h"

bool install_single_package(const Config& cfg, const PackageInfo& info, const fs::path& sp) {
    fs::path whl = cfg.spip_root / (info.name + "-" + info.version + ".whl");
    if (!fs::exists(whl) || fs::file_size(whl) == 0) {
        static std::mutex m_reg; static std::map<std::string, std::shared_ptr<std::mutex>> locks;
        std::shared_ptr<std::mutex> wheel_lock; { std::lock_guard<std::mutex> l(m_reg); if (locks.find(info.wheel_url) == locks.end()) locks[info.wheel_url] = std::make_shared<std::mutex>(); wheel_lock = locks[info.wheel_url]; }
        std::lock_guard<std::mutex> l(*wheel_lock);
        if (!fs::exists(whl) || fs::file_size(whl) == 0) {
            fs::path part = whl.string() + ".part." + std::to_string(getpid());
            bool quiet = (std::thread::hardware_concurrency() > 8);
            std::string dl = std::format("timeout 300 curl -f -L --connect-timeout 10 --max-time 240 -s {} {} -o {}", quiet ? "" : "-#", quote_arg(info.wheel_url), quote_arg(part.string()));
            if (run_shell(dl.c_str()) == 0 && fs::exists(part) && fs::file_size(part) > 0) fs::rename(part, whl);
            else { if (fs::exists(part)) fs::remove(part); return false; }
        }
    }
    fs::path helper = cfg.spip_root / "scripts" / "safe_extract.py"; fs::path py = cfg.project_env_path / "bin" / "python";
    std::string ext = std::format("{} {} {} {}", quote_arg(py.string()), quote_arg(helper.string()), quote_arg(whl.string()), quote_arg(sp.string()));
    int ret = run_shell(ext.c_str());
    if (ret != 0) {
        std::cerr << YELLOW << "⚠️ Extraction failed for " << info.name << ". Retrying hardened download..." << RESET << std::endl;
        if (fs::exists(whl)) fs::remove(whl);
        std::string dl = std::format("timeout 300 curl -f -L --connect-timeout 10 --max-time 240 -s -# {} -o {}", quote_arg(info.wheel_url), quote_arg(whl.string()));
        if (run_shell(dl.c_str()) == 0 && fs::exists(whl)) ret = run_shell(ext.c_str());
        if (ret != 0) { std::cerr << RED << "❌ Installation failed for " << info.name << "." << RESET << std::endl; if (fs::exists(whl)) fs::remove(whl); return false; }
    }
    return true;
}

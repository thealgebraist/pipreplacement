#include "spip_env.h"

void exec_with_setup(Config& cfg, std::function<void(Config&)> func) {
    setup_project_env(cfg);
    func(cfg);
}

bool require_args(const std::vector<std::string>& args, size_t min_count, const std::string& usage_msg) {
    if (args.size() < min_count) {
        std::cout << usage_msg << std::endl;
        return false;
    }
    return true;
}

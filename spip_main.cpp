#include "spip_env.h"
#include "spip_cmd.h"

void signal_handler(int) { g_interrupted = true; }

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
    Config cfg = init_config();
    run_command(cfg, args);
    return 0;
}

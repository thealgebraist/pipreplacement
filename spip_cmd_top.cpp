#include "spip_cmd.h"
#include "spip_top.h"

void run_command_top(Config&, const std::vector<std::string>& args) {
    bool refs = false; bool deps = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--references") refs = true;
        else if (args[i] == "--dependencies") deps = true;
    }
    show_top_packages(refs, deps);
}
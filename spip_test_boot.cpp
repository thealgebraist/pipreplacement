#include "spip_test.h"

void boot_environment(const Config& cfg, const std::string& script_path) {
    fs::path b = cfg.spip_root / "boot"; fs::path k = b / "vmlinuz"; fs::path i = b / "initrd.img";
    if (!fs::exists(k) || !fs::exists(i)) { std::cout << YELLOW << "⚠️ Minimal Linux kernel/initrd missing in " << b << RESET << std::endl; return; }
    std::cout << MAGENTA << "🚀 Booting virtualized environment for " << script_path << "..." << RESET << std::endl;
    std::string acc = "-accel kvm -cpu host";
#ifdef __APPLE__
    acc = "-accel hvf -cpu host";
#endif
    std::string cmd = std::format(
        "qemu-system-x86_64 {} -m 1G -nographic -kernel {} -initrd {} "
        "-virtfs local,path={},mount_tag=spip_env,security_model=none,id=spip_env "
        "-virtfs local,path={},mount_tag=project_root,security_model=none,id=project_root "
        "-append \"console=ttyS0 root=/dev/ram0 rw init=/sbin/init spip_script={}\" ",
        acc, quote_arg(k.string()), quote_arg(i.string()), quote_arg(cfg.project_env_path.string()), quote_arg(cfg.current_project.string()), quote_arg(script_path));
    run_shell(cmd.c_str());
}


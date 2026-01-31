#include "ResourceProfiler.h"

uintmax_t get_dir_size(const fs::path& p) {
    uintmax_t size = 0;
    std::error_code ec;
    if (fs::exists(p, ec) && fs::is_directory(p, ec)) {
        try {
            for (auto it = fs::recursive_directory_iterator(p, fs::directory_options::skip_permission_denied, ec);
                 it != fs::recursive_directory_iterator(); it.increment(ec)) {
                if (ec) break;
                if (fs::is_regular_file(*it, ec)) {
                    uintmax_t s = fs::file_size(*it, ec);
                    if (!ec) size += s;
                }
                ec.clear();
            }
        } catch (...) {}
    }
    return size;
}

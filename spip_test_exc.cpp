#include "spip_test.h"

std::string extract_exception(const std::string& output) {
    std::regex err_re(R"(([a-zA-Z0-9_\.]+(Error|Exception):.*))");
    auto words_begin = std::sregex_iterator(output.begin(), output.end(), err_re);
    auto words_end = std::sregex_iterator();
    std::string last_err = "";
    for (std::sregex_iterator i = words_begin; i != words_end; ++i) last_err = (*i)[1].str();
    return last_err;
}

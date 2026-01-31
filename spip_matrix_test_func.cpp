#include "spip_matrix_tester.h"

void matrix_test(const Config& cfg, const std::string& pkg, const std::string& custom_test_script, const std::string& python_version, bool profile, bool no_cleanup, int revision_limit, bool test_all_revisions, bool vary_python, int pkg_revision_limit, const std::string& pinned_pkg_ver) {
    MatrixTester tester(cfg, pkg);
    tester.run(custom_test_script, python_version, profile, no_cleanup, revision_limit, test_all_revisions, vary_python, pkg_revision_limit, pinned_pkg_ver);
}

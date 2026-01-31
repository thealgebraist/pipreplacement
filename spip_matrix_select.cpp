#include "spip_matrix_tester.h"
#include "spip_db.h"

std::vector<std::string> MatrixTester::select_versions(bool vary_python, int revision_limit, bool test_all_revisions, int pkg_revision_limit, const std::string& pinned_pkg_ver) {
    std::vector<std::string> v;
    if (vary_python) {
        v = {"3.13", "3.12", "3.11", "3.10", "3.9", "3.8", "3.7", "2.7"};
        if (revision_limit > 0 && v.size() > (size_t)revision_limit) v.resize(revision_limit);
        else if (v.size() > 3) v.resize(3);
        if (pkg_revision_limit > 1) {
            auto pv = get_all_versions(pkg); if (pv.size() > (size_t)pkg_revision_limit) pv.erase(pv.begin(), pv.begin() + (pv.size() - pkg_revision_limit));
            std::vector<std::string> comb; for (const auto& py : v) for (const auto& p : pv) comb.push_back(py + ":" + p);
            v = comb;
        }
    } else {
        if (!pinned_pkg_ver.empty()) v = {pinned_pkg_ver};
        else v = get_all_versions(pkg);
        if (!test_all_revisions) {
            if (revision_limit > 0 && v.size() > (size_t)revision_limit) v.erase(v.begin(), v.begin() + (v.size() - revision_limit));
            else if (v.size() > 5) v.erase(v.begin(), v.begin() + (v.size() - 5));
        }
    }
    return v;
}

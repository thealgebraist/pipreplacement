#pragma once
#include "spip_matrix.h"
#include "ResourceProfiler.h"
#include "TelemetryLogger.h"
#include "ErrorKnowledgeBase.h"

struct MatrixResult { std::string version; bool install; bool pkg_tests; bool custom_test; ResourceUsage stats; };
struct MatrixErrorLog { std::string version; std::string python; std::string output; };

class MatrixTester {
    const Config& cfg;
    std::string pkg;
    std::vector<MatrixResult> results;
    std::vector<MatrixErrorLog> error_logs;
public:
    MatrixTester(const Config& c, const std::string& p) : cfg(c), pkg(p) {}
    void run(const std::string& custom_test_script, const std::string& python_version, bool profile, bool no_cleanup, int revision_limit, bool test_all_revisions, bool vary_python, int pkg_revision_limit, const std::string& pinned_pkg_ver);
private:
    std::vector<std::string> select_versions(bool vary_python, int revision_limit, bool test_all_revisions, int pkg_revision_limit, const std::string& pinned_pkg_ver);
    void parallel_execution(const std::vector<std::string>& to_do, const fs::path& test_script, const std::string& python_version, bool profile, bool no_cleanup, bool vary_python);
    void summarize(bool profile);
};

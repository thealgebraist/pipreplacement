#include "spip_bundle.h"

void generate_setup_py(const fs::path& target_dir, const std::string& pkg_name) {
    std::vector<std::string> cpp_files, py_files;
    for (const auto& entry : fs::directory_iterator(target_dir)) {
        if (entry.path().extension() == ".cpp") cpp_files.push_back(entry.path().filename().string());
        else if (entry.path().extension() == ".py" && entry.path().filename().string() != "setup.py") {
            std::string fname = entry.path().filename().string();
            py_files.push_back(fname.substr(0, fname.length() - 3));
        }
    }
    if (cpp_files.empty()) return;
    std::ofstream os(target_dir / "setup.py");
    os << "from setuptools import setup, Extension\nimport os\n\n";
    os << "module = Extension('" << pkg_name << "_cpp', sources=[";
    for (size_t i = 0; i < cpp_files.size(); ++i) os << "'" << cpp_files[i] << "'" << (i == cpp_files.size() - 1 ? "" : ", ");
    os << "], extra_compile_args=['-std=c++23'])\n\n";
    os << "setup(name='" << pkg_name << "', version='0.1', ext_modules=[module], py_modules=[";
    for (size_t i = 0; i < py_files.size(); ++i) os << "'" << py_files[i] << "'" << (i == py_files.size() - 1 ? "" : ", ");
    os << "])\n";
}


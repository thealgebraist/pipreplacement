CXX = g++
CXXFLAGS = -std=c++23 -O3 -Wall -Wextra -pedantic -I.
LDFLAGS = -lsqlite3

SRCS = spip_globals.cpp spip_utils.cpp spip_utils_shell.cpp spip_utils_exec.cpp \
       ResourceProfiler.cpp get_dir_size.cpp \
       ErrorKnowledgeBase.cpp ErrorKnowledgeBase_store.cpp ErrorKnowledgeBase_lookup.cpp ErrorKnowledgeBase_fixes.cpp \
       TelemetryLogger.cpp TelemetryLogger_methods.cpp TelemetryLogger_loop.cpp \
       TelemetryLogger_sample_apple.cpp TelemetryLogger_sample_linux.cpp TelemetryLogger_log.cpp \
       spip_env.cpp spip_env_dirs.cpp spip_env_git.cpp spip_env_setup.cpp spip_env_helpers.cpp \
       spip_python.cpp spip_python_bin.cpp spip_python_base.cpp spip_python_req.cpp \
       spip_db.cpp spip_db_fetch.cpp spip_db_json.cpp spip_db_vers.cpp \
       spip_install_wheel.cpp spip_install_info.cpp spip_install_main.cpp spip_install_single.cpp \
       spip_install_ops.cpp spip_install_prune.cpp \
       spip_test_tree.cpp spip_test_pkg.cpp spip_test_boot.cpp spip_test_utils.cpp \
       spip_test_ai.cpp spip_test_trim.cpp spip_test_exc.cpp \
       spip_matrix_test_func.cpp spip_matrix_tester.cpp spip_matrix_tester_impl.cpp \
       spip_matrix_select.cpp spip_matrix_dl.cpp spip_matrix_resolve.cpp \
       spip_matrix_par.cpp spip_matrix_summary.cpp spip_matrix_bench.cpp \
       spip_distributed.cpp spip_worker.cpp spip_mirrors.cpp spip_cmd.cpp spip_main.cpp

OBJS = $(SRCS:.cpp=.o)

all: spip

spip: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) spip
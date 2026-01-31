CXX = g++
CXXFLAGS = -std=c++23 -O3 -Wall -Wextra -pedantic -I.
LDFLAGS = -lsqlite3

OBJS = spip_globals.o spip_utils.o spip_utils_shell.o spip_utils_exec.o \
       ResourceProfiler.o get_dir_size.o \
       ErrorKnowledgeBase.o ErrorKnowledgeBase_store.o ErrorKnowledgeBase_lookup.o ErrorKnowledgeBase_fixes.o \
       TelemetryLogger.o TelemetryLogger_methods.o TelemetryLogger_loop.o \
       TelemetryLogger_sample_apple.o TelemetryLogger_sample_linux.o TelemetryLogger_log.o \
       spip_env.o spip_env_dirs.o spip_env_git.o spip_env_setup.o spip_env_helpers.o \
       spip_env_cleanup.o spip_env_cleanup_envs.o \
       spip_python.o spip_python_bin.o spip_python_base.o spip_python_req.o \
       spip_db.o spip_db_fetch.o spip_db_json.o spip_db_vers.o \
       spip_install_wheel.o spip_install_info.o spip_install_main.o spip_install_single.o \
       spip_install_ops.o spip_install_prune.o \
       spip_test_tree.o spip_test_pkg.o spip_test_boot.o spip_test_utils.o \
       spip_test_ai.o spip_test_trim.o spip_test_exc.o \
       spip_matrix_test_func.o spip_matrix_tester.o spip_matrix_tester_impl.o \
       spip_matrix_select.o spip_matrix_dl.o spip_matrix_resolve.o \
       spip_matrix_par.o spip_matrix_summary.o spip_matrix_bench.o \
       spip_distributed.o spip_worker.o spip_mirrors.o \
       spip_top.o spip_top_refs.o \
       spip_cmd.o spip_cmd_install.o spip_cmd_maint.o spip_cmd_matrix.o \
       spip_cmd_top.o spip_cmd_uninstall.o \
       spip_bundle.o spip_bundle_gen.o spip_main.o

all: spip

spip: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) spip

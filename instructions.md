# Project: Smart Pip (spip) - A Safe Pip Replacement

**Developer**: Antigravity (The Solver)
**Task**: Build a pip replacement that avoids global installs, system overwrites, and manual virtualenv management.

## Components

1. **Model (Coq)**: `safety.v` - Formal proof of environment isolation.
2. **Implementation (C++23)**: `spip.cpp` - The CLI tool.
   - **Hardened Shell Execution**: Universal 'quote_arg' sanitization prevents command injection.
   - **Safe Extraction**: Python-based 'safe_extract.py' prevents path traversal in wheels.
   - **Stable Hashing**: FNV-1a mixing ensures deterministic project identifiers.
   - **Recursive Housekeeping**: Orphan pruning with empty directory cleanup.
   - **Cross-Platform Trim**: Support for both 'otool' (Mac) and 'ldd' (Linux).
   - **AI Code Review**: Deep architectural and safety analysis via Gemini Pro.
   - **Real-Time Security Audit**: Batch CVE/GHSA scanning via OSV API.
   - **Environment Portability**: Compressed .tgz freeze/archival.
3. **Documentation**: `README.md`.

## Features

- **Deterministic Isolation**: Formally proven Git-backed environment separation.
- **Self-Healing**: Automated syntax repair and type-check verification.
- **Archival Stability**: Immutable base versions with branch-based evolution.

## Current Status

- All critical security vulnerabilities (Shell Injection, Path Traversal) resolved.
- Architectural refinements (Stable Hashing, JSON hardening, Dependency Parsing) implemented.
- Tool fully verified on current codebase via 'spip review'.
- [x] implement `spip implement` to generate packages using an LLM agent (Gemini/Copilot) until tests pass.
- [x] implement local Ollama support for `spip implement`.
- [x] implement external script splitting for maintainability.
- [x] implement `spip boot` for virtualized QEMU execution.
- [x] implement `spip bundle` to wrap, install, and test general C++23 code as a Python package.
- [x] Refactor `spip.cpp` by identifying common subgraphs (code clones).
- [x] comprehensive subgraph isomorphism benchmark (8 methods: LP, IP, MILP, B&B, CP, SA, GD, Ullmann) with 60s timeout each.
- [x] Compare all functions and sub-blocks (subexpressions) using 8 distinct methods (Similarity/Iso) and generate a heatmap/report.
- [x] Search for similar functions/subexpressions using a BERT-like model (CodeBERT) and cosine similarity.
- [x] Tree pattern matching with alpha-equivalence (De Bruijn indices) for CSE detection.
- [x] Applied refactorings: 3 helper functions extracted (get_site_packages, require_args, exec_with_setup), ~61 lines saved.
- [x] Added 'spip top' and 'spip top --references' functionality.
- [x] Fixed compilation errors in `spip.cpp` (undefined `Cyan` -> `CYAN`, incomplete regex iteration logic).
- [x] Refactored `spip matrix` for high-performance parallel execution (std::thread pool, atomic task tracking, isolated worktrees).
- [x] Large-scale 64-package matrix stress test (10 versions each) to achieve ultimate robustness.
- [x] Implement 4s-burst resume logic for `spip matrix` to satisfy strict execution constraints.
- [x] **SQLite Error Knowledge Base**: Persistent storage of exceptions and learned fixes for self-healing tests.
- [x] **Bytecode Profiler (`spip profile`)**: Comprehensive .pyc analysis measuring disk usage, memory footprint, and cycle complexity.
    - [x] **AI Resource Optimization**: `spip profile <pkg> --ai` uses Gemini to review the top 32 hotspots and suggest improvements to lower resource use.
    - [x] **Redundancy Analysis**: Detects repeated constant subexpressions (e.g., BUILD_MAP, BUILD_TUPLE) and suggests singleton/static optimization.
- [x] **Resource Hog Study**: Conducted a study of 64 top packages to identify common resource hogs. Generated a PDF report with architectural suggestions.
- [x] **High-Performance Telemetry (`spip compat --telemetry`)**: 10Hz sampling of per-core CPU, memory, network, and disk I/O logged to SQLite.
- [x] **Dynamic Download Concurrency**: Automated benchmarking for optimal thread sizing based on network probe (1-32 threads).
- [x] **Hardened Parallel Matrix**: Robust filesystem operations with `std::error_code` and multi-layered exception handling for stable 800+ test runs.
- [x] **Ninja Build Integration**: Transitioned to Ninja for ultra-fast incremental builds.
- [x] **100+ Core Optimization**: 
    - [x] Replaced global download locks with granular per-wheel mutexes for 100% parallel dependency resolution.
    - [x] Implemented a `std::counting_semaphore` for Git operations to allow safely overlapping worktree creation (8 concurrent ops).
- [x] **Orchestration Benchmark (`spip bench`)**: A dedicated pre-flight tool to verify thread scheduling Efficiency and telemetry mapping on high-density servers.
    - [x] **Network Stress Test (`--network`)**: Benchmarks multiple mirror nodes (PyPI, Tsinghua, USTC, Aliyun, Baidu) and calculates optimal download concurrency using a strict 4s-per-op quota.
- [x] **Smoke Test Flag (`--smoke`)**: Added to `matrix` and `compat` to validate the parallel execution engine before initiating long-running test suites.
- [x] **Stall Prevention & Hardened I/O**: 
    - [x] Integrated `timeout` and `curl` reachability flags to prevent dead connections from hanging the parallel downloader.
    - [x] Configured `sqlite3_busy_timeout` (10s) to handle high-concurrency database contention on multi-core servers.
- [ ] **EPYC Class Scaling (Roadmap)**: 
    - [ ] Created `task.md` outlining the shift to multi-instance orchestration (SPIP-Master).
    - [ ] Proposed `tmpfs` integration for zero-latency Git worktrees.
    - [ ] Design for WAL-mode SQLite telemetry to support massive cross-process logging.

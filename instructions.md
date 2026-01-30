# Project: Smart Pip (spip) - A Safe Pip Replacement

**Developer**: Antigravity
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

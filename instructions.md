# Project: Smart Pip (spip) - A Safe Pip Replacement

**Developer**: Antigravity
**Task**: Build a pip replacement that avoids global installs, system overwrites, and manual virtualenv management.

## Components

1. **Model (Coq)**: `safety.v` - Formal proof that the installation, auditing, and pruning logic preserves global system state.
2. **Implementation (C++23)**: `spip.cpp` - The CLI tool.
   - Centralized Git repository for all environments.
   - `spip install <pkg>`: Resolve, install, verify, and track manual installs.
   - `spip uninstall <pkg>`: Precise file-level uninstallation.
   - **`spip audit`**: Performs a real-time security audit of all installed libraries by querying the **OSV (Open Source Vulnerability) database**. It identifies CVEs, summaries, and CVSS severity scores for the specific versions in the environment.
   - `spip prune`: Housekeeping to remove orphaned dependencies.
   - `spip test <pkg|--all>`: Automates the execution of package internal test suites.
   - `spip --freeze <out.tgz>`: Bundles the entire environment into a compressed archive.
   - `spip verify`: Explicitly run syntax, auto-repair, and recursive type checks.
   - `spip trim <script.py>`: Minimizes environment size.
3. **Documentation**: `README.md`.

## Features

- **Real-Time Security Auditing**: Direct integration with the OSV API to ensure that no library in the dependency graph has known critical vulnerabilities (CVEs).
- **Orphan Pruning**: Recursive cleanup of unused sub-dependencies.
- **Environment Portability**: Capture and share exact environment states.
- **Global Package Testing**: Bulk validation of library health.
- **Self-Healing Verification**: Automated repair of legacy Python syntax.
- **Safety**: Formally proven isolation from global system paths.

## Current Status

- Implemented **Security Auditing** (`audit`) with OSV API integration.
- Implemented **Orphan Pruning** for dependency cleanup.
- Implemented **Environment Freezing** for portability.
- Implemented `verify` and `test` suites.
- Git-backed environment management implemented.

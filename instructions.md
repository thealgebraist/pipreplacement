# Project: Smart Pip (spip) - A Safe Pip Replacement

**Developer**: Antigravity
**Task**: Build a pip replacement that avoids global installs, system overwrites, and manual virtualenv management.

## Components

1. **Model (Coq)**: `safety.v` - Formal proof that the installation, auditing, pruning, and **AI review** logic preserves global system state.
2. **Implementation (C++23)**: `spip.cpp` - The CLI tool.
   - Centralized Git repository for all environments.
   - `spip install <pkg>`: Resolve, install, verify, and track manual installs.
   - `spip uninstall <pkg>`: Precise file-level uninstallation.
   - **`spip review`**: Gathers all project source files (.py, .cpp, .v, etc.) and performs a deep architectural and safety review through the **Gemini Pro API**. Identifies design flaws, security risks, and optimization opportunities.
   - `spip audit`: Real-time security audit via OSV database.
   - `spip prune`: Housekeeping to remove orphaned dependencies.
   - `spip test <pkg|--all>`: Automates the execution of package internal test suites.
   - `spip --freeze <out.tgz>`: Bundles the entire environment into a compressed archive.
   - `spip verify`: Explicitly run syntax, auto-repair, and recursive type checks.
   - `spip trim <script.py>`: Minimizes environment size.
3. **Documentation**: `README.md`.

## Features

- **AI-Driven Code Review**: Built-in integration with state-of-the-art LLMs (Gemini Pro) to provide instant, high-quality feedback on project code.
- **Real-Time Security Auditing**: Direct integration with the OSV API.
- **Orphan Pruning**: Recursive cleanup of unused sub-dependencies.
- **Environment Portability**: Capture and share exact environment states.
- **Global Package Testing**: Bulk validation of library health.
- **Self-Healing Verification**: Automated repair of legacy Python syntax.
- **Safety**: Formally proven isolation from global system paths.

## Current Status

- Implemented **AI Code Review** (`review`) with Gemini Pro integration.
- Implemented **Security Auditing** (`audit`) with OSV API integration.
- Implemented **Orphan Pruning** and **Environment Freezing**.
- Git-backed environment management implemented.

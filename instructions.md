# Project: Smart Pip (spip) - A Safe Pip Replacement

**Developer**: Antigravity
**Task**: Build a pip replacement that avoids global installs, system overwrites, and manual virtualenv management.

## Components

1. **Model (Coq)**: `safety.v` - Formal proof that the installation logic preserves global system state and supports versioning via branching.
2. **Implementation (C++23)**: `spip.cpp` - The CLI tool.
   - Centralized Git repository for all environments.
   - Base branches for different Python versions.
   - Project-specific branches versioned via Git.
   - `spip install <pkg>`: Resolve dependencies and install from wheels (Git-committed).
   - `spip uninstall <pkg>`: Precisely remove packages using `RECORD` metadata.
   - `spip search <query>`: Fast local searching across the package index.
   - **`spip tree <pkg>`**: Visualize the full recursive dependency hierarchy.
   - `spip use <version>`: Switches the project to a specific Python version branch.
   - `spip fetch-db`: Sync the local PyPI metadata vault.
   - `spip shell` / `spip run`: Execute in the version-controlled environment.
3. **Documentation**: `README.md`.

## Features

- **Dependency Visualization**: Built-in tree viewer for understanding complex package hierarchies.
- **Instant Search**: Sub-millisecond searching across the 730k+ package index.
- **Deterministic Removal**: Uses package metadata records to ensure no orphaned files are left behind.
- **Rich UI**: Progress bars for individual file downloads and overall installation progress.
- **Local Registry**: Complete offline-capable metadata database stored in Git.
- **Isolation**: Environments stored in `~/.spip/repo`.
- **Safety**: Never uses `sudo`. System integrity is formally proven.
- **Versioning**: Instant switching and rollback via Git branches.

## Current Status

- Implemented `tree` command for recursive dependency visualization.
- Implemented `search` command with metadata enrichment.
- Implemented download progress bars.
- Implemented `uninstall` command with metadata record parsing.
- Implemented multi-threaded PyPI database fetcher.
- Git-backed environment management with worktrees implemented.

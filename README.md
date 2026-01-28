# spip - Smart Pip

A safer, version-controlled replacement for `pip` and `venv`.

## Features

- **Isolated Centralized Storage**: All environments live in `~/.spip/envs`, identified by project hash.
- **Git-Backed States**: Every change is automatically committed to a local Git repository.
- **Pure Implementation**: Does not rely on `pip` for installation. Includes a custom dependency resolver and wheel-based installer.
- **Local Registry**: Versioned Git-backed metadata library for PyPI packages.
- **Formally Verified Isolation**: Coq-modeled recursive dependency resolution and installation.
- **Branch-Based Multi-Python**: Python versions are stored as base branches.

## Commands

- `spip install <packages>`: Resolve, download, and install packages (Git-committed).
- `spip run <command>`: Run a command within the environment.
- `spip use <version>`: Switch project to a specific Python version.
- `spip fetch-db`: Sync the local PyPI metadata vault.
- `spip log`: Show environment change history.

## Design Philosophy

`spip` treats the entire environment as an immutable ledger. Installations are performed by resolving wheels against a local registry, unzipping them directly into a version-controlled worktree, and committing the resulting state.

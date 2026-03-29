# Envoy Tools Directory — Overview

The `tools/` directory contains the developer tooling ecosystem for the Envoy proxy project. It houses scripts, utilities, and automation that support code quality enforcement, API management, building, debugging, dependency tracking, IDE integration, and release workflows.

This documentation series explains what each tool does, when to use it, and how the pieces fit together.

---

## Directory Map

```
tools/
├── api/                          # API structure validation and Go protobuf generation
├── api_proto_breaking_change_detector/  # Detects breaking changes in API protos
├── api_proto_plugin/             # Protoc plugin framework for API annotations
├── api_versioning/               # API version header generation
├── base/                         # Shared Python tooling (Runner, Checker, envoy_py_binary)
├── bootstrap2pb/                 # Convert bootstrap YAML/JSON to text proto
├── cat/                          # Minimal cat-style build utility
├── ci/                           # CI reporting tools
├── clang-format/                 # Bazel rules for clang-format
├── clang-tidy/                   # Bazel rules for clang-tidy
├── code/                         # Code check test utilities
├── code_format/                  # Format checker and BUILD fixer
├── coverage/                     # Code coverage report generation
├── debugging/                    # Valgrind runner and suppression files
├── dependency/                   # Dependency validation, CVE tracking, OSSF scorecard
├── deprecate_features/           # Automated proto field deprecation
├── deprecate_guards/             # Deprecation guard lifecycle via GitHub API
├── dev/                          # Development Python dependencies
├── distribution/                 # Docker Hub metadata updater
├── envoy_collect/                # Collect stats, logs, and perf profiles from Envoy
├── extensions/                   # Extension schema definitions
├── git/                          # Git helper scripts
├── github/                       # GitHub-specific tools (source version, assignable sync)
├── h3_request/                   # HTTP/3 request tool (uses aioquic)
├── jq/                           # JQ processing utilities
├── proto_format/                 # Proto formatting and sync pipeline
├── protojsonschema/              # Proto-to-JSON schema generation
├── protojsonschema_with_aspects/ # Proto JSON schema with Bazel aspects
├── protoprint/                   # Proto pretty-printer
├── protoshared/                  # Shared proto Bazel rules
├── python/                       # Python tooling base
├── repo/                         # Reviewer notification (Slack/GitHub)
├── sha/                          # SHA replacement utility
├── socket_passing/               # Socket passing for hot restart tests
├── spelling/                     # Spelling checker with custom dictionary
├── tarball/                      # Zstd tarball unpacker
├── testdata/                     # Test inputs for format/proto/spelling tools
├── toolchain/                    # Toolchain detection
├── type_whisperer/               # Type database generation from proto descriptors
├── vscode/                       # VS Code debug config and setup guides
└── docs/                         # This documentation series
```

---

## Tool Categories

The tools fall into eight logical categories:

| # | Category | Key Directories | Doc |
|---|----------|----------------|-----|
| 1 | **Code Quality** | `code_format/`, `spelling/`, `clang-format/`, `clang-tidy/`, `code/` | [02-Code-Quality-Tools.md](02-Code-Quality-Tools.md) |
| 2 | **API & Proto** | `api/`, `api_proto_plugin/`, `api_proto_breaking_change_detector/`, `proto_format/`, `protoprint/`, `type_whisperer/`, `api_versioning/` | [03-API-and-Proto-Tools.md](03-API-and-Proto-Tools.md) |
| 3 | **Build & CI** | `base/`, `ci/`, `bootstrap2pb/`, `toolchain/`, top-level scripts | [04-Build-and-CI-Tools.md](04-Build-and-CI-Tools.md) |
| 4 | **Debugging & Profiling** | `debugging/`, `envoy_collect/`, `stack_decode.py` | [05-Debugging-and-Profiling-Tools.md](05-Debugging-and-Profiling-Tools.md) |
| 5 | **Dependency Management** | `dependency/`, `extensions/`, `update_crates.sh`, `check_repositories.sh` | [06-Dependency-Management-Tools.md](06-Dependency-Management-Tools.md) |
| 6 | **IDE & Editor** | `vscode/`, `gen_compilation_database.py`, `find_related_envoy_files.py`, `path_fix.sh`, `envoy-rotate-files.el` | [07-IDE-and-Editor-Tools.md](07-IDE-and-Editor-Tools.md) |
| 7 | **GitHub, Release & Misc** | `github/`, `repo/`, `distribution/`, `git/`, `deprecate_features/`, `deprecate_guards/`, `sha/`, `h3_request/`, `tarball/`, `socket_passing/`, `coverage/` | [08-GitHub-Release-and-Misc-Tools.md](08-GitHub-Release-and-Misc-Tools.md) |
| 8 | **Adding New Tools** | `base/`, top-level `README.md` | [09-Adding-New-Tools.md](09-Adding-New-Tools.md) |

---

## Shared Infrastructure

### `tools/base/` — The Foundation

Most Python tools are built on top of two base classes defined in `tools/base/`:

- **`Runner`** — Base class for CLI tools with argument parsing. Subclass it, implement `add_arguments()` and `run()`, and you get help menus, logging, and a standardized entry point.
- **`Checker`** — Base class for linting/checking tools. Define a `checks` tuple and implement `check_<name>()` methods. Results are categorized as `succeed`, `warn`, or `error` and summarized automatically.

The `envoy_py_binary` Bazel macro (from `tools/base/envoy_python.bzl`) wraps Python tools with automatic test targets (`pytest_<name>`) and dependency management.

### `tools/run_command.py`

A lightweight library for running shell commands and capturing output, used by several tools internally.

### `tools/testdata/`

Contains test fixtures for `check_format`, `protoxform`, `api_proto_breaking_change_detector`, and `spelling` tools.

---

## Quick Reference: Common Developer Tasks

| I want to... | Tool / Command |
|--------------|---------------|
| Fix formatting on my changes | `tools/local_fix_format.sh` |
| Check code format | `bazel run //tools/code_format:check_format` |
| Generate `compile_commands.json` | `tools/gen_compilation_database.py` or `ci/do_ci.sh refresh_compdb` |
| Decode a stack trace | `bazel run //tools:stack_decode` |
| Collect debug artifacts from Envoy | `tools/envoy_collect/envoy_collect.py --envoy-binary ... --output-path ...` |
| Generate VS Code debug config | `tools/vscode/generate_debug_config.py //source/exe:envoy-static --args "..."` |
| Validate dependencies | `bazel run //tools/dependency:validate` |
| Check for API breaking changes | `bazel run //tools/api_proto_breaking_change_detector:detector` |
| Run tests in Docker | `tools/bazel-test-docker.sh //test/...` |
| Profile build times | `tools/build_profile.py` |
| Send an HTTP/3 request | `bazel run //tools/h3_request:h3_request` |
| Check spelling | `bazel run //tools/spelling:check_spelling_pedantic` |
| Jump between related files (editor) | `tools/find_related_envoy_files.py <file>` |

---

## Navigation

| Document | Description |
|----------|-------------|
| **[01-Overview.md](01-Overview.md)** | This document — high-level map and quick reference |
| **[02-Code-Quality-Tools.md](02-Code-Quality-Tools.md)** | Format checking, linting, spelling |
| **[03-API-and-Proto-Tools.md](03-API-and-Proto-Tools.md)** | Proto plugins, formatting, breaking change detection |
| **[04-Build-and-CI-Tools.md](04-Build-and-CI-Tools.md)** | Build helpers, CI scripts, compilation database |
| **[05-Debugging-and-Profiling-Tools.md](05-Debugging-and-Profiling-Tools.md)** | Stack decoder, Valgrind, envoy_collect |
| **[06-Dependency-Management-Tools.md](06-Dependency-Management-Tools.md)** | Dependency validation, CVE tracking, extensions schema |
| **[07-IDE-and-Editor-Tools.md](07-IDE-and-Editor-Tools.md)** | VS Code, Emacs, compilation DB, file navigation |
| **[08-GitHub-Release-and-Misc-Tools.md](08-GitHub-Release-and-Misc-Tools.md)** | GitHub ops, deprecation, distribution, coverage |
| **[09-Adding-New-Tools.md](09-Adding-New-Tools.md)** | Step-by-step guide to adding a new Python tool |

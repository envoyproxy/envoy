# Code Quality Tools

This document covers the tools that enforce coding standards, formatting rules, and spelling across the Envoy codebase.

---

## Overview

Envoy maintains strict code quality through automated checkers that run both locally and in CI. These tools ensure consistent formatting across C++, Bazel BUILD files, protobuf definitions, and documentation.

```
tools/
├── code_format/          # Main format checker and BUILD fixer
│   ├── check_format.py   # Central format enforcement script
│   ├── config.yaml       # Rules, path exclusions, regex patterns
│   └── envoy_build_fixer.py  # Automated BUILD file corrections
├── clang-format/         # Bazel rules wrapping clang-format
│   └── clang_format.bzl
├── clang-tidy/           # Bazel rules wrapping clang-tidy
│   └── clang_tidy.bzl
├── spelling/             # Spelling checker
│   ├── check_spelling_pedantic.py
│   └── spelling_dictionary.txt
├── code/                 # Test helper for check output
│   └── check_test.sh
└── local_fix_format.sh   # Quick-fix formatting on local changes
```

---

## `tools/code_format/` — Format Checker

### `check_format.py`

The central code quality gatekeeper. It checks files against a comprehensive set of rules defined in `config.yaml`.

**What it checks:**

- **C++ formatting** via `clang-format` — ensures consistent brace style, spacing, and alignment
- **BUILD file formatting** via `buildifier` — enforces canonical Bazel BUILD syntax
- **Include order** — validates C++ `#include` directives follow the project's ordering convention (`envoy` → `common` → `source` → `exe` → `server` → `extensions` → `test`)
- **Banned patterns** — catches usage of forbidden APIs:
  - `std::string_view` (must use `absl::string_view` except in WASM code)
  - `std::regex` (restricted to specific files)
  - `Protobuf::util::MessageDifferencer` (use the Envoy wrapper)
  - Direct `grpc_init` (only allowed in one file)
  - `memcpy` (use `safe_memcpy` instead)
  - Old-style `MOCK_METHOD` (must use modern form)
- **Exception usage** — only files explicitly allowlisted in `config.yaml` may throw exceptions; new code should use `StatusOr`
- **Namespace conventions** — validates namespace structure
- **Runtime guard naming** — checks `RUNTIME_GUARD` flag format
- **Test naming** — ensures test names start with uppercase
- **Real-time system usage** — restricts `RealTimeSystem` to allowlisted files
- **Histogram naming** — enforces SI suffix rules for stats

**Usage:**

```bash
# Check all files
bazel run //tools/code_format:check_format

# Check specific files
bazel run //tools/code_format:check_format -- check /path/to/file.cc
```

### `config.yaml`

The configuration file that drives `check_format.py`. It defines:

- **`suffixes`** — file extensions to check (`.cc`, `.h`, `.proto`, `.bzl`, `BUILD`, etc.)
- **`paths.excluded`** — directories skipped during checks (e.g., `third_party/`, `mobile/`, `generated/`)
- **`paths.exception`** — files allowed to throw C++ exceptions
- **`paths.protobuf`** — files allowed to use protobuf APIs directly
- **`paths.real_time`** — files allowed to reference real-world time
- **`paths.std_regex`** — files allowed to use `std::regex`
- **`paths.std_string_view`** — files allowed to use `std::string_view`
- **`re`** — regex patterns used for detection rules
- **`replacements`** — automatic code convention fixes (e.g., `.Times(1);` → `;`)
- **`dir_order`** — canonical include directory ordering
- **`visibility_excludes`** — BUILD files exempt from visibility checks

### `envoy_build_fixer.py`

Automatically fixes common BUILD file issues:

- Corrects load statement ordering
- Fixes dependency formatting
- Ensures consistent BUILD file structure

**Usage:**

```bash
bazel run //tools/code_format:envoy_build_fixer -- /path/to/BUILD
```

---

## `tools/clang-format/` — C++ Formatting Rules

Provides Bazel macros (`clang_format.bzl`) that wrap `clang-format` for use in Bazel build rules. The project-wide `.clang-format` file at the repository root defines the style configuration.

These rules are consumed by the CI pipeline and by `check_format.py` rather than invoked directly by developers.

---

## `tools/clang-tidy/` — Static Analysis Rules

Provides Bazel macros (`clang_tidy.bzl`) for integrating `clang-tidy` static analysis into the build. Clang-tidy catches subtle C++ issues like:

- Use-after-move
- Dangling references
- Modernization opportunities (e.g., `auto`, range-based for loops)
- Performance issues

---

## `tools/spelling/` — Spelling Checker

### `check_spelling_pedantic.py`

Checks source code comments and documentation for spelling errors against a custom dictionary.

**Usage:**

```bash
bazel run //tools/spelling:check_spelling_pedantic
```

### `spelling_dictionary.txt`

The custom dictionary of allowed words — includes Envoy-specific terminology, protocol names, abbreviations, and technical jargon that would otherwise be flagged by standard spell checkers.

To add a new word, append it to `spelling_dictionary.txt` in alphabetical order.

---

## `tools/local_fix_format.sh` — Quick Format Fix

The developer's best friend for pre-commit formatting. This script runs all format fixers on your uncommitted changes.

**Usage:**

```bash
# Fix formatting on uncommitted changes
tools/local_fix_format.sh

# Fix formatting on changes since diverging from main
tools/local_fix_format.sh -main

# Fix specific files
tools/local_fix_format.sh source/common/http/conn_manager_impl.cc
```

---

## `tools/code/check_test.sh`

A minimal test helper that fails if its input file is non-empty. Used by CI to assert that check tools produce no output (i.e., no violations found).

---

## Typical Developer Workflow

1. Make code changes
2. Run `tools/local_fix_format.sh` to auto-fix formatting
3. Review any remaining issues that can't be auto-fixed
4. If adding new files with exceptions or special patterns, update `tools/code_format/config.yaml`
5. Push — CI runs `check_format.py` and `check_spelling_pedantic.py` automatically

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| — | [01-Overview.md](01-Overview.md) | [03-API-and-Proto-Tools.md](03-API-and-Proto-Tools.md) |

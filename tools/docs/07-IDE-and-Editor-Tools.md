# IDE and Editor Tools

This document covers the tools that integrate Envoy's build system with IDEs and code editors for a productive development experience.

---

## Overview

Envoy's Bazel-based build produces artifacts in sandboxed directories that standard IDE tooling can't navigate. These tools bridge that gap by generating compilation databases, debug configurations, and file navigation helpers.

```
tools/
├── vscode/                            # VS Code / Cursor support
│   ├── generate_debug_config.py       # Generate .vscode/launch.json entries
│   ├── README.md                      # VS Code setup guide
│   └── README_SCRIPT.md              # Detailed script documentation
├── gen_compilation_database.py        # Generate compile_commands.json
├── find_related_envoy_files.py        # Find related source/header/test files
├── path_fix.sh                        # Translate Bazel paths to real paths
└── envoy-rotate-files.el             # Emacs file rotation hook
```

---

## `tools/gen_compilation_database.py` — Compilation Database

The single most important tool for IDE integration. Generates `compile_commands.json` at the workspace root, which clangd and other language servers use to understand the project's build configuration.

**What it provides:**
- Correct include paths (including Bazel-generated code)
- Compiler flags matching the actual build
- Proper symbol resolution across the entire codebase
- Support for Bazel external dependencies

**Usage:**

```bash
# Direct invocation
python3 tools/gen_compilation_database.py

# Via CI script (recommended — also builds proto dependencies)
ci/do_ci.sh refresh_compdb
```

The `ci/do_ci.sh refresh_compdb` method is strongly recommended because it:
1. Builds all protobuf-generated code first
2. Fetches external dependencies
3. Then generates the compilation database

Without the generated proto code, clangd will report missing headers for any proto-derived `.pb.h` files.

**When to regenerate:**
- After changing `.proto` files
- After modifying `BUILD` files or Bazel structure
- After pulling significant upstream changes
- When clangd reports missing headers or incorrect diagnostics

**IDE recommendation:** Disable the Microsoft C/C++ extension and use the `vscode-clangd` extension instead. Clangd provides superior performance and accuracy for large C++ codebases like Envoy.

---

## `tools/vscode/` — VS Code / Cursor Support

### `generate_debug_config.py`

Generates debug launch configurations in `.vscode/launch.json` for interactive debugging of Envoy binaries and tests.

**What it does:**

1. Builds the specified Bazel target in debug mode (`-c dbg`)
2. Reads or creates `.vscode/launch.json` at the workspace root
3. Adds a debug configuration with the correct binary path, source mapping, and command-line arguments
4. Writes the updated `launch.json` (backing up the original)

**Usage:**

```bash
# Generate GDB config for Envoy binary
tools/vscode/generate_debug_config.py //source/exe:envoy-static \
  --args "-c envoy/network/envoy.yaml"

# Generate LLDB config (default on macOS)
tools/vscode/generate_debug_config.py //source/exe:envoy-static \
  --debugger lldb \
  --args "-c envoy/network/envoy.yaml"

# Generate config for a test target
tools/vscode/generate_debug_config.py //test/common/http:conn_manager_impl_test

# Overwrite existing config for same target
tools/vscode/generate_debug_config.py //source/exe:envoy-static \
  --args "-c envoy/network/envoy.yaml" \
  --overwrite
```

**Generated config names:** `<debugger type> <bazel target>` (e.g., `gdb //source/exe:envoy-static`)

**Debugger extensions required:**
- GDB: [Native Debug](https://marketplace.visualstudio.com/items?itemName=webfreak.debug) extension
- LLDB: [VSCode LLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) extension

**Important notes:**
- The `--args` value is what gets passed to Envoy at runtime (e.g., `-c config.yaml`), not to the debugger
- Without `--overwrite`, re-running the script only updates binary paths and source maps — existing `args` are preserved
- On Apple Silicon (ARM64), source mapping is simplified to `".": "${workspaceFolder}"`

### Source Mapping

The generated configs include source path mappings so the debugger can translate Bazel sandbox paths to your workspace:

- `/proc/self/cwd` → workspace root
- `bazel-out/...` → Bazel execution root

This ensures breakpoints work correctly and "go to source" lands on the right file.

### Recommended VS Code Setup

See the [devcontainer](../../.devcontainer/README.md) for the recommended development environment. Key points:

- Use the `vscode-clangd` extension (not Microsoft C/C++)
- Run `ci/do_ci.sh refresh_compdb` to generate the compilation database
- Use `generate_debug_config.py` to set up debug configurations

---

## `tools/find_related_envoy_files.py` — Related File Finder

Given a source file, outputs the related files in the Envoy source tree. This powers editor hotkeys for jumping between related files.

**Relationships detected:**

| Current File | Related Files |
|-------------|---------------|
| `source/common/http/foo.cc` | `source/common/http/foo.h`, `test/common/http/foo_test.cc` |
| `source/common/http/foo.h` | `source/common/http/foo.cc`, `test/common/http/foo_test.cc` |
| `test/common/http/foo_test.cc` | `source/common/http/foo.cc`, `source/common/http/foo.h` |

**Usage:**

```bash
python3 tools/find_related_envoy_files.py source/common/http/conn_manager_impl.cc
# Output: source/common/http/conn_manager_impl.h
#         test/common/http/conn_manager_impl_test.cc
```

**IDE integration:** Bind this to a keyboard shortcut in your editor. For VS Code, you can create a task that runs this script and opens the first result.

---

## `tools/envoy-rotate-files.el` — Emacs File Rotation

An Emacs Lisp hook that rotates between related Envoy source files (implementation ↔ header ↔ test) with a single keystroke. The Emacs equivalent of `find_related_envoy_files.py`.

**Installation:** Add to your `.emacs` or `init.el`:

```elisp
(load "/path/to/envoy/tools/envoy-rotate-files.el")
;; Bind to a key, e.g.:
(global-set-key (kbd "C-c r") 'envoy-rotate-file)
```

---

## `tools/path_fix.sh` — Path Translator

Translates Bazel sandbox paths in compiler error output to real filesystem paths that IDEs can click on.

**Problem:** Bazel error messages look like:

```
bazel-out/k8-fastbuild/bin/external/com_google_protobuf/_virtual_includes/...
```

These paths don't exist on the real filesystem in a way IDEs can navigate.

**Solution:**

```bash
bazel build //source/exe:envoy-static 2>&1 | tools/path_fix.sh
```

Now error paths point to actual files in the workspace or Bazel output directories.

---

## Quick Setup Guide

For a new developer setting up their IDE:

1. **Clone and build once** to fetch all dependencies:
   ```bash
   bazel build //source/exe:envoy-static
   ```

2. **Generate the compilation database:**
   ```bash
   ci/do_ci.sh refresh_compdb
   ```

3. **Install clangd extension** in VS Code/Cursor (disable Microsoft C/C++)

4. **Generate debug config** (optional):
   ```bash
   tools/vscode/generate_debug_config.py //source/exe:envoy-static \
     --args "-c envoy/network/envoy.yaml"
   ```

5. **Reload VS Code/Cursor** — clangd should start indexing

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| [06-Dependency-Management-Tools.md](06-Dependency-Management-Tools.md) | [01-Overview.md](01-Overview.md) | [08-GitHub-Release-and-Misc-Tools.md](08-GitHub-Release-and-Misc-Tools.md) |

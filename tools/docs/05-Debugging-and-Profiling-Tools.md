# Debugging and Profiling Tools

This document covers the tools that help developers debug Envoy issues and profile its performance.

---

## Overview

Debugging a high-performance proxy like Envoy requires specialized tools for decoding stack traces, collecting runtime artifacts, and running under memory analysis tools.

```
tools/
├── debugging/                         # Valgrind support
│   ├── run-valgrind.sh                # Run tests under Valgrind
│   └── valgrind-suppressions.txt      # Known false-positive suppressions
├── envoy_collect/                     # Runtime artifact collector
│   ├── envoy_collect.py               # Wrapper to gather logs, stats, profiles
│   └── README.md
└── stack_decode.py                    # Stack trace address resolver
```

---

## `tools/stack_decode.py` — Stack Trace Decoder

When Envoy crashes, the stack trace typically contains raw memory addresses rather than human-readable function names. `stack_decode.py` resolves these addresses to source-level symbols using `addr2line`.

**What it does:**

1. Reads a stack trace from stdin (or a file)
2. Extracts memory addresses from each frame
3. Invokes `addr2line` against the Envoy binary to resolve addresses to `file:line` + function name
4. Outputs a decoded, human-readable stack trace

**Usage:**

```bash
# Decode from clipboard / stdin
pbpaste | bazel run //tools:stack_decode -- /path/to/envoy-static

# Decode from a crash log file
bazel run //tools:stack_decode -- /path/to/envoy-static < crash.log
```

**Prerequisites:** The Envoy binary must have been built with debug symbols (`-c dbg` or `--copt=-g`) for meaningful output. Without debug info, `addr2line` will only show addresses without file/line information.

**Tip:** When building for debugging, use:

```bash
bazel build -c dbg //source/exe:envoy-static
```

---

## `tools/envoy_collect/` — Runtime Artifact Collector

A wrapper script that runs Envoy and automatically collects a bundle of debugging or profiling artifacts when the process is interrupted.

### Debugging Mode

Collects verbose logs, stats, and admin endpoint data:

```bash
tools/envoy_collect/envoy_collect.py \
  --envoy-binary /path/to/envoy-static \
  --output-path /path/to/debug.tar \
  -c /path/to/envoy-config.json \
  <other Envoy args...>
```

The wrapper:
1. Starts Envoy with maximum logging verbosity
2. Runs Envoy as normal (proxying traffic, etc.)
3. When interrupted via `SIGINT` (ctrl-c or `kill -s INT`), captures:
   - All log output
   - Stats dump from the admin endpoint
   - Server info, cluster status, listener status
   - Config dump
   - Other admin endpoint handler outputs
4. Packages everything into the specified tarball

### Performance Mode

Additionally captures a `perf` profile:

```bash
tools/envoy_collect/envoy_collect.py \
  --performance \
  --envoy-binary /path/to/envoy-static \
  --output-path /path/to/perf-debug.tar \
  -c /path/to/envoy-config.json
```

In performance mode, Envoy runs under `perf record`, and the tarball includes:
- Everything from debugging mode (excluding verbose logging to avoid performance impact)
- A `perf.data` file for analysis with `perf report` or flamegraph tools

### Privacy Warning

The debug tarball contains detailed traffic-level information. Exercise extreme caution before sharing:
- **Do not** attach raw tarballs to public GitHub issues if there are any privacy concerns
- Manually sanitize data before posting in public view
- Prefer sharing only specific, relevant excerpts

### Analyzing Output

```bash
# Extract the tarball
tar xf debug.tar

# View stats
cat stats.txt

# View config dump
cat config_dump.json

# For performance mode: generate a flamegraph
perf script -i perf.data | stackcollapse-perf.pl | flamegraph.pl > flamegraph.svg
```

---

## `tools/debugging/` — Valgrind Support

### `run-valgrind.sh`

Runs Envoy test targets under [Valgrind](https://valgrind.org/) to detect memory errors:

- Use-after-free
- Memory leaks
- Uninitialized value reads
- Invalid memory accesses
- Buffer overflows

**Usage:**

```bash
tools/debugging/run-valgrind.sh //test/common/http:conn_manager_impl_test
```

The script configures Valgrind with:
- `--leak-check=full` for comprehensive leak detection
- Envoy-specific suppressions from `valgrind-suppressions.txt`
- Appropriate error exit codes for CI integration

### `valgrind-suppressions.txt`

Contains suppression rules for known false positives — memory patterns that Valgrind flags but are intentional (e.g., third-party library allocations, global singletons that live for the process lifetime).

When Valgrind reports a false positive from a third-party library or a known-safe pattern, add a suppression entry here rather than ignoring the report.

---

## Debugging Tips

### Building for Debug

```bash
# Full debug build (slow but complete symbol info)
bazel build -c dbg //source/exe:envoy-static

# Debug build with specific config (e.g., clang)
bazel build -c dbg --config=clang //source/exe:envoy-static
```

### Using GDB / LLDB

For interactive debugging, see [07-IDE-and-Editor-Tools.md](07-IDE-and-Editor-Tools.md) for VS Code debug config generation. For command-line debugging:

```bash
# GDB
gdb --args bazel-bin/source/exe/envoy-static -c envoy-config.yaml

# LLDB (macOS)
lldb -- bazel-bin/source/exe/envoy-static -c envoy-config.yaml
```

### Common Debugging Workflows

| Scenario | Tool |
|----------|------|
| Crash with stack trace in logs | `stack_decode.py` |
| Need logs + stats + config from a running instance | `envoy_collect.py` (debug mode) |
| Performance investigation / flamegraph | `envoy_collect.py --performance` |
| Suspected memory corruption or leak | `run-valgrind.sh` |
| Interactive breakpoint debugging | GDB/LLDB via VS Code config |

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| [04-Build-and-CI-Tools.md](04-Build-and-CI-Tools.md) | [01-Overview.md](01-Overview.md) | [06-Dependency-Management-Tools.md](06-Dependency-Management-Tools.md) |

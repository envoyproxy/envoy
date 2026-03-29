# generate_debug_config.py — Full explanation

## Where does the script write?

The script **does** write at the project root. It always writes to:

```
<Bazel workspace root>/.vscode/launch.json
```

- `workspace` = output of `bazel info workspace` (your repo root, e.g. `/workspaces/envoy`).
- So the file is **`.vscode/launch.json`** in the repo root, not in `tools/vscode/`.

It does **not** generate a config *file* path for Envoy. The `-c envoy.yaml` (or `-c envoy/network/envoy.yaml`) is the **command-line argument** stored in that launch config — i.e. “run Envoy with this config path”. That value comes from the script’s **`--args`** (e.g. from the README example `--args "-c envoy.yaml"`). So:

- **Script output location:** repo root → `.vscode/launch.json` (correct).
- **Why you saw “Invalid path: envoy.yaml”:** the *content* of `--args` was `-c envoy.yaml`, and `envoy.yaml` does not exist at repo root; the real file is `envoy/network/envoy.yaml`. So the fix is either to pass `--args "-c envoy/network/envoy.yaml"` when running the script, or to edit `launch.json` after (as we did).

---

## What the script does (overview)

1. Builds the given Bazel target in **debug** mode (`-c dbg`).
2. Reads or creates `.vscode/launch.json` at the **workspace root**.
3. Adds or updates **one** debug configuration (GDB or LLDB) for that target, with the binary path, source mapping, and any `--args` you passed.
4. Writes `launch.json` back (after backing up the existing one).

It does **not** generate `envoy.yaml` or any config file; it only generates the VS Code/Cursor **launch** config that tells the debugger which binary to run and which arguments (e.g. `-c envoy/network/envoy.yaml`) to pass.

---

## Line-by-line walkthrough

### Environment and globals (lines 12–13)

```python
BAZEL_OPTIONS = shlex.split(os.environ.get("BAZEL_BUILD_OPTION_LIST", ""))
BAZEL_STARTUP_OPTIONS = shlex.split(os.environ.get("BAZEL_STARTUP_OPTION_LIST", ""))
```

- Optional extra flags for `bazel build` and `bazel info` (e.g. from CI or local env).
- If not set, both are empty lists.

---

### Bazel helpers

**`bazel_info(name, bazel_extra_options=[])` (16–18)**  
- Runs `bazel info <name>` with startup + global options + `bazel_extra_options`.  
- Returns the single line of output (e.g. workspace path, `bazel-bin`, execution root).  
- Used to find workspace root, `bazel-bin`, and execution root.

**`get_workspace()` (20–22)**  
- Returns `bazel info workspace` → repo root path.

**`get_execution_root(workspace)` (25–32)**  
- Prefer: read `compile_commands.json` at workspace root and use the first entry’s `directory` (Bazel execution root). This keeps breakpoints/source mapping in sync with clangd.  
- Fallback: `bazel info execution_root`.

**`binary_path(bazel_bin, target)` (35–38)**  
- Converts a Bazel target like `//source/exe:envoy-static` into the path under `bazel-bin`, e.g. `bazel_bin/source/exe/envoy-static` (with `@` → `external/`).  
- So the script knows the path to the built binary.

---

### Build (lines 41–55)

**`build_binary_with_debug_info(target, config=None)`**

- Runs `bazel build -c dbg <target>` (and optional `--config=...`, e.g. `clang`).
- Then gets `bazel info bazel-bin` for that config and builds the full binary path.
- Returns that path so the launch config can use the **debug** binary.

---

### launch.json read/write (lines 58–71)

**`get_launch_json(workspace)`**  
- Path: `workspace/.vscode/launch.json`.  
- Returns parsed JSON, or `{"version": "0.2.0"}` if missing/unreadable.

**`write_launch_json(workspace, launch)`**  
- Path again: `workspace/.vscode/launch.json`.  
- If `launch.json` exists, moves it to `launch.json.bak`.  
- Writes the new `launch` dict as formatted JSON.  
- So the **only** file the script creates/updates is **`.vscode/launch.json` at the workspace (repo) root**.

---

### Config builders (74–107)

**`gdb_config(...)`**  
- Builds a VS Code launch entry for **GDB**: `target` (binary path), `arguments` (string passed through), `debugger_args` (e.g. `--directory=<execroot>`), `cwd`, etc.  
- `arguments` is the raw string you pass to `--args` (GDB extension uses it as one string).

**`lldb_config(...)`**  
- Builds a launch entry for **LLDB**: `program` (binary), `args` = **`shlex.split(arguments)`** (so `-c envoy.yaml` → `["-c", "envoy.yaml"]`), `cwd`, and **sourceMap** so paths like `/proc/self/cwd` and `bazel-out` map to your workspace and exec root.  
- On Darwin ARM64, overrides sourceMap to `".": "${workspaceFolder}"` (workaround for that platform).

So:

- **Where** the file goes: **always** `workspace/.vscode/launch.json` (repo root).  
- **What** ends up as `-c envoy.yaml`: whatever you pass in **`--args`** (e.g. `-c envoy.yaml` from the README example). The script does not invent that; it only stores it in the generated config.

---

### Merging into launch.json (110–140)

**`add_to_launch_json(target, binary, workspace, execroot, arguments, debugger_type, overwrite)`**

- Loads existing `launch` from `workspace/.vscode/launch.json`.
- Builds `new_config` via `gdb_config` or `lldb_config` (so `arguments`/`args` come from the script’s `--args`).
- Looks for an existing configuration with the same **name** (e.g. `lldb //source/exe:envoy-static`).
  - If **overwrite**: replace that entry entirely with `new_config`.
  - If **not overwrite**: only update `always_overwritten_fields` (e.g. `program`, `sourceMap`, `cwd` for LLDB). **`args` is not in that list for LLDB**, so existing `args` (e.g. `["-c", "envoy.yaml"]`) are **left unchanged** when you re-run without `--overwrite`.
- If no matching name exists, appends `new_config`.
- Writes the result back with `write_launch_json(workspace, launch)` → again, **repo root** `.vscode/launch.json`.

So:

- **First run** with `--args "-c envoy.yaml"` → config is created with `args: ["-c", "envoy.yaml"]`.  
- **Later run** without `--args` and without `--overwrite` → only program/sourceMap/cwd etc. change; **args stay** `["-c", "envoy.yaml"]`. That’s why you can see `envoy.yaml` in the file even if you didn’t pass `--args` the last time.

---

### Main (157–185)

- Parses: `target`, `--debugger`, `--args`, `--config`, `--overwrite`.
- Optionally picks a Bazel config (e.g. `clang` on ARM64).
- Gets workspace and execution root.
- Builds the target in debug.
- Calls `add_to_launch_json(..., args.args, ...)` so whatever you passed as `--args` (or the default `''`) is what gets into the launch config.

Default for `--args` is **empty**, so if you run:

```bash
tools/vscode/generate_debug_config.py //source/exe:envoy-static
```

the new config gets **no** args (`[]`). If an old config with the same name exists and you don’t use `--overwrite`, its `args` (e.g. `["-c", "envoy.yaml"]`) are kept.

---

## Summary

| Question | Answer |
|----------|--------|
| Where does the script generate the file? | In the **repo root**: `workspace/.vscode/launch.json` (it does **not** write under `tools/vscode/`). |
| Does it generate a config file for Envoy (e.g. envoy.yaml)? | No. It only generates the **VS Code launch config** and puts whatever you pass in `--args` (e.g. `-c envoy.yaml`) into that config. |
| Why did I see “Invalid path: envoy.yaml”? | Because the launch config had `args: ["-c", "envoy.yaml"]` (from a previous run with `--args "-c envoy.yaml"` or from the README example), and `envoy.yaml` does not exist at repo root; the real file is `envoy/network/envoy.yaml`. |
| How do I fix it for future runs? | Run the script with the correct path, e.g.:  
`tools/vscode/generate_debug_config.py //source/exe:envoy-static --args "-c envoy/network/envoy.yaml"`  
and use `--overwrite` if you want to replace the existing config’s args. |

So: the script **does** generate in the “root folder” (repo root’s `.vscode/launch.json`). The “envoy.yaml” you saw was not a file the script created; it was the **Envoy config path** stored in the launch config, coming from `--args` (or from a previously saved config when not using `--overwrite`).

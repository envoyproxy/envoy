# Adding New Tools

This document is a step-by-step guide for adding new Python tools to the Envoy tooling ecosystem. It distills the process from the main `tools/README.md` and adds practical advice.

---

## Overview

Envoy tools are Python scripts managed through Bazel. The key infrastructure is:

- **`tools/base/envoy_python.bzl`** — Defines `envoy_py_binary` (runnable tool + auto-generated test target)
- **`tools/base/runner.py`** — `Runner` base class for CLI tools
- **`tools/base/checker.py`** — `Checker` base class for linting/validation tools
- **`pytest`** — All tools must have unit tests

---

## Step-by-Step: Adding a New Tool

### 1. Decide Where It Lives

If your tool fits an existing category, add it to that directory:

| Category | Directory |
|----------|-----------|
| Code quality | `tools/code_format/` |
| API/proto | `tools/api/`, `tools/proto_format/` |
| Dependencies | `tools/dependency/` |
| Debugging | `tools/debugging/` |
| GitHub/repo | `tools/github/`, `tools/repo/` |

If it's a new category, create a new directory under `tools/` (e.g., `tools/mytool/`).

### 2. Create Python Dependencies (If Needed)

If your tool needs Python packages not already available in an existing `requirements.txt`:

**a. Create `requirements.in` with unpinned dependencies:**

```
# tools/mytool/requirements.in
requests
pyyaml>=5.0
```

**b. Generate pinned `requirements.txt` with hashes:**

```bash
pip install pip-tools
pip-compile --generate-hashes tools/mytool/requirements.in -o tools/mytool/requirements.txt
```

**c. Register in Bazel** — add to `bazel/repositories_extra.bzl`:

```starlark
pip_install(
    name = "mytool_pip3",
    requirements = "@envoy//tools/mytool:requirements.txt",
    extra_pip_args = ["--require-hashes"],
)
```

**d. Add to Dependabot** — add to `.github/dependabot.yml`:

```yaml
- package-ecosystem: "pip"
  directory: "/tools/mytool"
  schedule:
    interval: "daily"
```

If your tool only uses packages already available in another tool's requirements, skip this step and reference that tool's `requirement()` function instead.

### 3. Write the Tool

#### Simple Tool (using `Runner`)

```python
#!/usr/bin/env python3

import sys
from tools.base.runner import Runner


class MyTool(Runner):

    def add_arguments(self, parser):
        parser.add_argument("input", help="Input file to process")
        parser.add_argument("--verbose", action="store_true")

    def run(self) -> int:
        # self.args contains parsed arguments
        # Return 0 for success, non-zero for failure
        return 0


def main(*args) -> int:
    return MyTool(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
```

#### Checker Tool (using `Checker`)

```python
#!/usr/bin/env python3

import sys
from tools.base.checker import Checker


class MyChecker(Checker):
    checks = ("format", "structure", "naming")

    def check_format(self) -> None:
        # self.args.path contains the target path
        issues = find_format_issues(self.args.path)
        if issues:
            self.error("format", issues)
        else:
            self.succeed("format", ["All files formatted correctly"])

    def check_structure(self) -> None:
        # ...
        pass

    def check_naming(self) -> None:
        # ...
        pass


def main(*args) -> int:
    return MyChecker(*args).run()


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:]))
```

`Checker` tools automatically accept a `--path` argument (or positional path) that specifies the directory to check.

### 4. Create the BUILD File

```starlark
load("//tools/base:envoy_python.bzl", "envoy_py_binary")
load("@mytool_pip3//:requirements.bzl", "requirement")

licenses(["notice"])  # Apache 2

envoy_py_binary(
    name = "tools.mytool.mytool",
    deps = [
        "//tools/base:runner",          # or "//tools/base:checker"
        requirement("requests"),
        requirement("pyyaml"),
    ],
)
```

The `name` must be the full dotted module path. `envoy_py_binary` automatically:
- Creates a runnable target `:mytool`
- Creates a test target `:pytest_mytool`

### 5. Write Unit Tests

Create `tools/mytool/tests/test_mytool.py`:

```python
from tools.mytool import mytool


def test_mytool_main(patches):
    patched = patches(
        "Runner.run",
        prefix="tools.mytool.mytool")

    with patched as (m_run,):
        m_run.return_value = 0
        assert mytool.main() == 0
```

#### The `patches` Fixture

Envoy provides a `patches` pytest fixture that simplifies mock setup:

```python
def test_something(patches):
    patched = patches(
        "os.path.exists",
        "json.dumps",
        prefix="tools.mytool.mytool")

    with patched as (m_exists, m_dumps):
        m_exists.return_value = True
        m_dumps.return_value = "{}"
        # test code here
```

This is equivalent to nested `unittest.mock.patch` calls but much cleaner.

### 6. Make It Runnable Without Bazel (Optional)

```bash
chmod +x tools/mytool/mytool.py
```

Users can then run directly:

```bash
./tools/mytool/mytool.py --help
```

They'll need to have Python dependencies installed locally.

### 7. Run and Test

```bash
# Run the tool
bazel run //tools/mytool:mytool -- --help

# Run tests
bazel run //tools/mytool:pytest_mytool

# Run tests with verbose output
bazel run //tools/mytool:pytest_mytool -- -v
```

### 8. Debug with Breakpoints

Add `breakpoint()` anywhere in your code or tests to drop into `pdb`:

```python
def run(self) -> int:
    data = load_data()
    breakpoint()  # will pause here
    return process(data)
```

---

## File Structure Summary

After following all steps, your tool directory looks like:

```
tools/mytool/
├── BUILD
├── mytool.py
├── requirements.in        # (if new dependencies)
├── requirements.txt       # (if new dependencies)
└── tests/
    └── test_mytool.py
```

---

## Checklist

- [ ] Tool has a `main()` function and `if __name__ == "__main__"` guard
- [ ] Uses `Runner` or `Checker` base class (or has a good reason not to)
- [ ] `BUILD` file uses `envoy_py_binary` with full dotted name
- [ ] Dependencies are pinned with hashes in `requirements.txt`
- [ ] Unit tests exist at `tests/test_<name>.py`
- [ ] Tests pass: `bazel run //tools/mytool:pytest_mytool`
- [ ] Tool runs: `bazel run //tools/mytool:mytool -- --help`
- [ ] Dependabot entry added (if new requirements)
- [ ] Bazel repository registered (if new requirements)

---

## Navigation

| Previous | Up | Next |
|----------|------|------|
| [08-GitHub-Release-and-Misc-Tools.md](08-GitHub-Release-and-Misc-Tools.md) | [01-Overview.md](01-Overview.md) | — |

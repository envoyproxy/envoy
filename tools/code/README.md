# tools/code

Utilities for checking and maintaining code quality in the Envoy repository.

## `narrow_test_mocks` — Narrow over-broad test-mock includes

### Background

The `test/mocks/server/` directory contains a pyramid of mock headers with
a broad-to-narrow dependency chain:

```
server_mocks (mocks.h)            ← broadest, pulls in everything
  └─ factory_context_mocks        ← pulls in instance_mocks
       └─ instance_mocks          ← pulls in server_factory_context_mocks
            └─ server_factory_context_mocks  ← narrowest base
```

Because the broader headers include the narrower ones transitively, a test
that only needs `MockServerFactoryContext` can accidentally include
`test/mocks/server/instance.h` or even `test/mocks/server/mocks.h` and still
compile.  This drags in huge transitive dependency closures (secret manager,
XDS manager, cluster manager, TLS context libraries, …), causing:

- Slow test builds and unnecessary cache busting.
- Out-of-memory failures on CI (especially for resource-constrained executors).
- Harder-to-read test code (why is a simple config test touching server
  lifecycle notifiers?).

### What the tool does

For every `*_test.cc` (and adjacent `.h`) under `test/`, `contrib/`, and
`mobile/test/`:

1. **Parses** which `Mock*` symbols from the `test/mocks/server/` family are
   actually referenced.
2. **Maps** the symbol set to the minimum set of headers/Bazel deps that expose
   all of them (never widening an already-narrow include).
3. In **fix mode**, rewrites the `#include` list in the source file and the
   matching `deps` list in the `BUILD` file.

### Usage

```bash
# Check — report files that can be narrowed; exit 1 if any found
bazel run //tools/code:narrow_test_mocks

# Fix — apply rewrites in place
bazel run //tools/code:narrow_test_mocks -- --mode fix

# Verbose output (shows which symbols trigger each change)
bazel run //tools/code:narrow_test_mocks -- --mode check --verbose

# Restrict to a specific subtree
bazel run //tools/code:narrow_test_mocks -- --mode fix \
    --paths test/extensions/filters/http

# Run the unit tests
bazel test //tools/code:narrow_test_mocks_test
```

### Adding rules for new mock families

The narrowing logic is driven by a `FAMILY_RULES` dict at the top of
`narrow_test_mocks.py`.  Adding support for a new mock family (e.g.
`test/mocks/network/`) requires extending that dict with a new key:

```python
FAMILY_RULES["network"] = {
    "symbol_to_header_dep": {
        "MockConnection": (
            "test/mocks/network/connection.h",
            "//test/mocks/network:connection_mocks",
        ),
        # … add all symbols …
    },
    "broad_headers": {
        "test/mocks/network/mocks.h": (
            "//test/mocks/network:network_mocks",
            frozenset(),  # no own symbols
        ),
        # …
    },
    "dep_dominates": { … },
    "include_dominates": { … },
}
```

Then pass `--rules server network` on the command line.

### Safety guarantees

- The tool **never widens** an include: if the existing include is already the
  narrowest possible, it is left unchanged.
- Files containing `using namespace` directives in the server-mock scope are
  skipped (conservative fallback, noted with `--verbose`).
- Proto-generated files and build-system files are never touched.
- BUILD rewriting preserves all existing non-server-mock deps unchanged and
  keeps the list sorted alphabetically to satisfy `buildifier`.

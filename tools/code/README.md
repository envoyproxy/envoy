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

### Transitive-include safety check

When narrowing from a broad server-mock header (e.g. `instance.h`) to a
narrower sibling (e.g. `admin.h`), the script may inadvertently remove
symbols that the file relied upon via the *broader* header's transitive
include chain.  For example, `instance.h` transitively pulls in HTTP mock
headers; `admin.h` does not.  A file that uses
`Http::MockStreamDecoderFilterCallbacks` without its own
`#include "test/mocks/http/mocks.h"` line would silently break.

To prevent this, the script maintains a `TRANSITIVE_GUARD` table (at the top
of `narrow_test_mocks.py`) that maps regex patterns for at-risk non-server
symbols to the direct include that must be present for the narrowing to be
safe:

| Pattern | Required direct include |
|---|---|
| `Http::Mock*` | `test/mocks/http/mocks.h` |
| `Http::Test*HeaderMap*` / `Http::Test*TrailerMap*` | `test/test_common/utility.h` |
| `Api::Mock*` | `test/mocks/api/mocks.h` |
| `Api::createApiForTest` | `test/test_common/utility.h` |
| `Network::Mock*` | `test/mocks/network/mocks.h` |
| `Upstream::Mock*` | `test/mocks/upstream/mocks.h` |
| `Stats::Mock*` | `test/mocks/stats/mocks.h` |
| `Runtime::Mock*` | `test/mocks/runtime/mocks.h` |
| `Tracing::Mock*` | `test/mocks/tracing/mocks.h` |
| `Singleton::ManagerImpl` | `source/common/singleton/manager_impl.h` |
| `TestUtility::*` / `TestEnvironment::*` | `test/test_common/utility.h` |

**Default (safe) behaviour**: if any guarded symbol is used but its required
direct include is missing, the entire narrowing for that file is skipped and a
diagnostic is emitted with `--verbose`.  The file is left untouched.

To fix a skipped file, manually add the required direct `#include` and Bazel
dep listed in the table, then re-run the script — it will then narrow
successfully.

To extend the guard for new at-risk symbol classes, add entries to the
`TRANSITIVE_GUARD` list at the top of `narrow_test_mocks.py`.

# compat/openssl — Guide for AI Agents

Instructions for AI agents adapting the OpenSSL compatibility layer when new BoringSSL
symbols are needed (typically after bumping gRPC, BoringSSL, or other deps).

See `README.md` in this directory for the full architectural background.

## Architecture in brief

Envoy is built against the BoringSSL API. The compat layer lets it run on OpenSSL instead.

1. **Prefixer** (`prefixer/prefixer.cpp`) copies OpenSSL headers, adding an `ossl_` prefix
   to every identifier. Output lands in `include/ossl/openssl/*.h`. It also generates
   `source/ossl.c` (forwarding functions via dlsym) and `include/ossl.h` (the `ossl` struct
   with function pointers for every real OpenSSL function).

2. **Patched BoringSSL headers** (`patch/include/openssl/*.h.sh`) start by commenting out the
   entire BoringSSL header, then selectively uncomment the symbols the compat layer exposes.
   The `uncomment.sh` tool handles this. The output is `include/openssl/*.h`.

3. **Mapping functions** (`source/*.c` or `source/*.cc`) implement each exposed BoringSSL
   function by calling the `ossl_`-prefixed OpenSSL equivalent.

4. **Patched BoringSSL sources and tests** (`patch/crypto/**`, `patch/ssl/**`) work like the
   headers: a few BoringSSL `.cc` files (`patched_bssl_sources` in `BUILD`) and BoringSSL's
   own unit tests (`test/BUILD`) are copied and selectively uncommented. For each file,
   `<file>.patch` (if present) is applied first with `patch`, then `<file>.sh` (if present)
   runs. A file with neither is entirely commented out. See `bazel/rules.bzl`.

The BoringSSL sources come from the `@boringssl-source` repo
(`bazel-envoy/external/boringssl-source+/`), **not** `@boringssl`. OpenSSL headers come
from `bazel-envoy/external/openssl+/include/openssl/`.

## Key files to modify

| File | Purpose |
|------|---------|
| `patch/include/openssl/<header>.h.sh` | Controls which symbols from BoringSSL's `<header>.h` are exposed |
| `patch/<path>.sh` / `patch/<path>.patch` | Patch a BoringSSL source or test file (e.g. `crypto/bytestring/cbs.cc`, `crypto/x509/x509_test.cc`) |
| `BUILD` | The `mapping_func_filegroup` list — every exposed function must be listed here |
| `source/<function>.c` or `.cc` | Handwritten mapping when auto-generation won't work |
| `test/test_*.cc` | Compat-specific unit tests (e.g. `test/test_stack.cc`) |

`compat/openssl/` is excluded from `check_format` (`tools/code_format/config.yaml`), so
follow the style of neighbouring files rather than running `clang-format`.

## How to add a missing function

### Step 1: Uncomment the declaration

Add `--uncomment-func-decl <function_name>` to the appropriate `.h.sh` patch script.

Example in `ssl.h.sh`:
```bash
  --uncomment-func-decl SSL_get_negotiated_group \
```

### Step 2: Add to the BUILD file

Add the function name to the `mapping_func_filegroup` list (alphabetically sorted within
its section).

### Step 3: Decide if a handwritten source file is needed

The build system (`bazel/rules.bzl`) auto-generates a forwarding function if no handwritten
`source/<function>.c` or `.cc` exists. The generated code handles both cases — OpenSSL
macros and real functions — using an `#ifdef`:

```c
// Auto-generated pattern:
ReturnType FunctionName(args) {
#ifdef ossl_FunctionName
  return ossl_FunctionName(args);        // macro path (expands inline)
#else
  return ossl.ossl_FunctionName(args);   // function pointer path (via dlsym)
#endif
}
```

**You need a handwritten source when:**
- The BoringSSL and OpenSSL signatures differ (different arg types, arg count)
- The semantics differ (e.g., `SSL_CTX_set1_curves_list` has an OpenSSL 3.5 bug workaround)
- The function has no OpenSSL equivalent at all (must be implemented from scratch)

**You can rely on auto-generation when:**
- The function exists in OpenSSL with the same signature (as a real function or macro)
- No semantic differences need patching

## How to add a missing constant or macro

### Constant exists in both BoringSSL and OpenSSL

Add `--uncomment-macro-redef '<pattern>'` to the `.h.sh` patch script. This generates:

```c
#ifdef ossl_CONSTANT_NAME
#define CONSTANT_NAME ossl_CONSTANT_NAME
#endif
```

The constant gets OpenSSL's value. Use regex patterns to cover families:
```bash
  --uncomment-macro-redef 'SSL_R_[[:alnum:]_]*' \
  --uncomment-macro-redef 'OPENSSL_INIT_[[:alnum:]_]*' \
```

### Constant exists only in BoringSSL (no OpenSSL equivalent)

The `--uncomment-macro-redef` approach won't work because there's no `ossl_` version — the
`#ifdef` guard will be false and the constant stays undefined.

Instead, append a standalone `#ifndef`/`#define` block at the end of the `.h.sh` script:

```bash
cat >> "$1" <<'EOF'

#ifndef SSL_R_SOME_BORINGSSL_ONLY_CONSTANT
#define SSL_R_SOME_BORINGSSL_ONLY_CONSTANT <value>
#endif
EOF
```

**Choosing values:** BoringSSL and OpenSSL often use the same numeric range for different
constants. For example, BoringSSL's `SSL_R_NO_CIPHERS_PASSED = 176` collides with OpenSSL's
`SSL_R_NO_CERTIFICATES_RETURNED = 176`. If both appear in the same switch statement,
you get a duplicate-case error. To avoid this, use values in a range that neither library
uses (e.g., 10000+ for `SSL_R_*` constants). The exact values don't matter at runtime
since OpenSSL will never produce these BoringSSL-specific error codes.

### Constant with duplicate-case-value problem

If a constant must exist but its value collides with another constant's value (e.g.,
`ERR_R_OVERFLOW` aliased to `ERR_R_INTERNAL_ERROR`), give it a unique value. Check
OpenSSL's range for the constant family in `bazel-envoy/external/openssl+/include/openssl/`
and pick a value above the highest used one.

## Adapting to upstream refactors in BoringSSL

Not every failure is a missing symbol. BoringSSL often renames or moves code, which breaks
the patch scripts themselves (they match on BoringSSL's text). Common cases:

- **A helper moved to another file / internal header** (e.g. `add_decimal` in `cbs.cc`
  became `bssl::cbb_add_decimal_ascii` in `cbb.cc`, declared in `crypto/bytestring/internal.h`).
  Internal headers are usually commented out, so uncomment the new definition and add a
  forward declaration with `--sed` in the patch scripts that use it.
- **A template/class was reworked** (e.g. `StackAllocated` became traits based, with
  `BORINGSSL_MAKE_STACK_TRAITS`). Uncomment the new pieces, including the matching
  specializations in other headers (e.g. `BORINGSSL_MAKE_STACK_TRAITS(CBB, ...)` in
  `bytestring.h`).
- **A `.patch` hunk no longer applies** (context changed). Apply the hunk by hand to the
  new source, then regenerate with `diff -u` (keep the `a/<file>` / `b/<file>` headers).
- **A test uses new constants or helpers** (e.g. `kTestCertSerial`). Uncomment them in the
  test's `.sh`. Tests skipped with `--uncomment-gtest-func-skip` must still compile, so
  BoringSSL-only constants they reference still need a definition.

## uncomment.sh — common options

| Option | Effect |
|--------|--------|
| `--comment` | Comment out the whole file (always first) |
| `-h` | Uncomment header boilerplate: preprocessor directives, `extern "C"`, `BSSL_NAMESPACE_*` |
| `--uncomment-func-decl <name>` | Uncomment an `OPENSSL_EXPORT` function declaration |
| `--uncomment-func-impl <name>` | Uncomment a function definition (ends at `}` in column 0) |
| `--uncomment-static-func-impl <name>` | Same, for `static` functions |
| `--uncomment-macro '<pattern>'` | Uncomment a `#define` (keeps BoringSSL's value) |
| `--uncomment-macro-redef '<pattern>'` | Redefine macro to use OpenSSL's value via `ossl_` prefix |
| `--uncomment-enum <name>` | Uncomment an enum definition |
| `--uncomment-struct <name>` | Uncomment a struct definition |
| `--uncomment-class <name>` | Uncomment a class definition |
| `--uncomment-using <name>` | Uncomment a `using` alias |
| `--uncomment-typedef <name>` | Uncomment a typedef |
| `--uncomment-typedef-redef <name>` | Redefine a typedef to use OpenSSL's type |
| `--uncomment-regex '<re>' ['<re>' ...]` | One regex: uncomment every matching line. Several: uncomment the first run of consecutive lines matching them in order |
| `--uncomment-regex-range '<start>' '<end>'` | Uncomment from the first `<start>` match to the next `<end>` match |
| `--uncomment-gtest-func <suite> <name>` | Uncomment a BoringSSL test |
| `--uncomment-gtest-func-skip <suite> <name>` | Same, but `GTEST_SKIP()` it under `BSSL_COMPAT` |
| `--comment-regex '<re>'` | Re-comment lines matching a regex |
| `--sed '<expression>'` | Run an arbitrary sed expression on the file |

Patterns are matched against the commented-out text (`// ` prefix is added implicitly), using
basic regex syntax (`\s`, `\(...\)`, `\|`). An error like `Failed to locate first pattern` or
`Error while processing option ...` means the BoringSSL text no longer matches.

## Validating patch scripts without a full build

Patch scripts are plain bash, so they can be checked against the new BoringSSL sources in
seconds. Run this from `compat/openssl/` after the new `@boringssl-source` has been fetched;
it reports every script or `.patch` that fails, not just the first one bazel hits:

```bash
S=$(bazel info output_base)/external/boringssl-source+
T=$(mktemp -d)
for p in $(git ls-files patch | grep '\.sh$' | sed 's%^patch/%%'); do
  f=${p%.sh}
  [ -f "$S/$f" ] || { echo "MISSING SRC: $f"; continue; }
  mkdir -p "$T/$(dirname $f)"; cp "$S/$f" "$T/$f"; chmod +w "$T/$f"
  if [ -f "patch/$f.patch" ]; then
    patch -s -f "$T/$f" "patch/$f.patch" -o "$T/$f.p" >/dev/null 2>&1 || echo "PATCH FAIL: $f"
    mv "$T/$f.p" "$T/$f"
  fi
  out=$(PATH="$PWD/tools:$PATH" bash "patch/$p" "$T/$f" 2>&1) || echo "FAIL $f: $out"
done
echo "output in $T"
```

`MISSING SRC` entries are stale scripts for files that no longer exist upstream; they only
matter if the file is still referenced in `BUILD` or `test/BUILD`. This catches script
breakage only. Compile errors (missing declarations, etc.) still need a bazel build.

To see what changed upstream, diff the affected files against the previous BoringSSL
version (the old version is in the `MODULE.bazel` diff of the bump commit), e.g.
`https://raw.githubusercontent.com/google/boringssl/<old-version>/<path>`.

## Inspecting generated output

To see what the compat layer actually produces after patching/prefixing, look in the bazel
output directory. The exact path depends on the build configuration:

```
bazel-out/k8-fastbuild/bin/compat/openssl/include/openssl/<header>.h   # patched BoringSSL header
bazel-out/k8-fastbuild/bin/compat/openssl/include/ossl/openssl/<header>.h  # prefixed OpenSSL header
bazel-out/k8-fastbuild/bin/compat/openssl/include/ossl.h              # ossl struct definition
bazel-out/k8-fastbuild/bin/compat/openssl/source/<function>.c         # auto-generated mapping
```

Check the prefixed OpenSSL headers to determine:
- Whether an `ossl_<symbol>` exists (i.e., whether `--uncomment-macro-redef` will work)
- Whether a symbol is a macro or a real function in OpenSSL
- What numeric value OpenSSL assigns to a constant

Check the `ossl.h` struct to see which OpenSSL functions are available as function pointers
(only real functions, not macros).

## Typical workflow for fixing build errors after a dep bump

1. **Run the patch-script validation loop above** and fix all script/`.patch` failures
   first. Diff against the previous BoringSSL version to understand each change.

2. **Read the compile errors.** Group them by type: undeclared functions, undeclared
   constants, duplicate case values. Errors pointing at a BoringSSL macro expansion (e.g.
   `DEFINE_STACK_OF` calling a new `OPENSSL_sk_last`) usually mean a new function that every
   expansion of that macro now needs.

3. **For each undeclared function:**
   - Check if it exists in BoringSSL (`bazel-envoy/external/boringssl-source+/include/openssl/`)
   - Check if it exists in OpenSSL (`bazel-envoy/external/openssl+/include/openssl/`)
   - Add `--uncomment-func-decl` to the patch script + entry in BUILD
   - If OpenSSL's semantics differ, or it has no OpenSSL equivalent, write a handwritten
     source file (and a test in `test/`)

4. **For each undeclared constant:**
   - Check if it exists in both BoringSSL and OpenSSL
   - If yes: use `--uncomment-macro-redef` in the patch script
   - If BoringSSL-only: append a `#ifndef`/`#define` with a collision-free value

5. **For duplicate case values:**
   - Identify which constants share the same numeric value
   - Give the BoringSSL-only constant a unique value outside both libraries' ranges

6. **Build and iterate.** New symbols may trigger further missing-symbol errors as
   more code becomes reachable. Build both Envoy (`--config=openssl`) and the compat tests
   (`//compat/openssl/test:utests-bssl-compat`), since the tests compile patched BoringSSL
   test files that Envoy itself does not use.

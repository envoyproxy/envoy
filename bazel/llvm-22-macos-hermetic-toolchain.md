# Research: macOS CI failure with the hermetic LLVM 22 toolchain

Status of this document: research findings for keeping the hermetic
(`toolchains_llvm`) compiler working on macOS after the bump to LLVM 22
(https://github.com/envoyproxy/envoy/pull/46297).

## TL;DR

The failure is **not** caused by supplying the LLVM 22 downloads out of band
(`extra_llvm_distributions`) — that part works. It is a **libc++
header/library ABI mismatch** that is baked into the macOS toolchain
configuration of `toolchains_llvm` up to and including the v1.7.0 release
Envoy pins today, and it is triggered by any hermetic LLVM ≥ 21.

On macOS, `toolchains_llvm` ≤ 1.7.0 compiles with the **hermetic toolchain's
bundled libc++ headers** but links against the **Xcode SDK's system
`libc++.tbd`**. LLVM 21 moved libc++'s hash implementation out of line,
introducing a new exported symbol `std::__1::__hash_memory` that the LLVM
21/22 headers reference unconditionally. The macOS 15 / Xcode 16.1 system
libc++ predates that symbol, so linking fails.

This is fixed upstream in `toolchains_llvm` **v1.8.0**
(commit [`68822c1`](https://github.com/bazel-contrib/toolchains_llvm/commit/68822c109475b725f3fe89f79bb51b8a3f3ccd23),
PR [#748](https://github.com/bazel-contrib/toolchains_llvm/pull/748)), which
makes macOS use the SDK's libc++ **end to end** (headers *and* library) —
i.e. exactly what the host Apple clang does, which is why the host compiler
works. **Upgrading `toolchains_llvm` to v1.8.0 (or backporting that commit
onto our patched v1.7.0) is what is required to keep the hermetic toolchain
working on macOS.**

## The failure

From the macOS CI job for PR #46297 (run
[30012052053](https://github.com/envoyproxy/envoy/actions/runs/30012052053),
commit `4d99a20`, `macos-15-xlarge` / arm64, Xcode 16.1):

```
ERROR: .../com_google_protobuf/src/google/protobuf/compiler/BUILD.bazel:258:10:
  Linking .../protoc_minimal [for tool] failed: (Exit 1): cc_wrapper.sh failed
  ...
  external/llvm_toolchain/bin/cc_wrapper.sh @bazel-out/darwin_arm64-opt-exec.../protoc_minimal-0.params

ld64.lld: error: undefined symbol: std::__1::__hash_memory(void const*, unsigned long)
>>> referenced by .../abseil-cpp/absl/strings/_objs/cord/cord_analysis.o
>>> referenced by .../abseil-cpp/absl/time/internal/cctz/_objs/time_zone/time_zone_impl.o
clang: error: linker command failed with exit code 1
```

Notes on the failure signature:

- It fails on the very first *host/exec-configuration* C++ binary that gets
  linked (`protoc_minimal`), so nothing Envoy-specific is involved — any
  hash-container-using C++ code hits it.
- The undefined symbol is `std::__1::__hash_memory`, i.e. a libc++ *internal
  ABI* symbol, referenced from `unordered_map`/`__hash_table` template
  instantiations compiled from the new headers.

## Root cause chain

1. **LLVM 21 changed libc++'s ABI surface.** libc++ moved the
   Murmur2/CityHash hashing implementation out of the headers and into the
   built library as a new exported function `std::__1::__hash_memory` (to fix
   ODR issues). Every hash-container instantiation compiled with LLVM ≥ 21
   libc++ headers emits a call to this symbol, expecting the *linked* libc++
   to provide it. See
   [llvm/llvm-project#155531](https://github.com/llvm/llvm-project/issues/155531)
   and [llvm/llvm-project#155606](https://github.com/llvm/llvm-project/issues/155606)
   (the exact protobuf failure mode we hit).

2. **`toolchains_llvm` ≤ v1.7.0 mixes libc++ versions on macOS.** With the
   default `builtin-libc++` stdlib on a darwin exec+target pair,
   `toolchain/cc_toolchain_config.bzl`:
   - lets Clang's driver auto-detect the **toolchain's bundled libc++
     headers** (found adjacent to the hermetic `clang` binary), i.e. the
     LLVM 22 headers; but
   - at link time deliberately adds `-L<sdk-sysroot>/usr/lib -lc++ -lc++abi`
     so libc++/libc++abi are **dynamically linked from the macOS SDK**. The
     upstream comment explains why: several macOS system libraries
     dynamically link libc++, so statically linking the toolchain's libc++
     into binaries is a known footgun on darwin.

   The sysroot is auto-detected via `xcrun --show-sdk-path`, i.e. the Xcode
   16.1 `MacOSX.sdk`, whose `libc++.tbd` corresponds to an older libc++ that
   has no `__hash_memory`. Headers say "call `__hash_memory`", the library
   doesn't have it → undefined symbol.

3. **LLVM 18.1.8 only worked by luck.** The LLVM 18 libc++ headers happen not
   to reference any symbol missing from the macOS 15 SDK libc++, so the
   header/library version skew was latent. Any hermetic LLVM ≥ 21 exposes it.
   This is tracked upstream as
   [bazel-contrib/toolchains_llvm#666](https://github.com/bazel-contrib/toolchains_llvm/issues/666)
   ("builtin-libc++ not working on macos", reported against LLVM 21.1.6 with
   the identical `__hash_memory` error).

### Why the host compiler works

Apple clang (or a Homebrew LLVM used as `LLVM_PATH`) uses the **SDK's own
libc++ headers together with the SDK's libc++ library** — a self-consistent
pair. The hermetic toolchain uses the same linker inputs but newer headers.
So "they should be the ~same" is almost true: the only delta is *which libc++
headers are used at compile time*, and that delta is the entire bug.

### The out-of-band downloads are a red herring

`extra_llvm_distributions` works fine in v1.7.0 (the attribute exists and the
archive was downloaded, extracted and used — the build compiles hundreds of
files and fails only at link). Also, `toolchains_llvm` v1.8.0 already knows
the official `LLVM-22.1.x-macOS-ARM64.tar.xz` distributions up to 22.1.7 in
its built-in table, so upstream *has* "set up" LLVM 22; only 22.1.8 is newer
than the v1.8.0 table and still needs the out-of-band hashes.

## Upstream state

- **Issue:** [toolchains_llvm#666](https://github.com/bazel-contrib/toolchains_llvm/issues/666)
  — closed as fixed by commit `68822c1`.
- **Fix:** commit
  [`68822c109475b725f3fe89f79bb51b8a3f3ccd23`](https://github.com/bazel-contrib/toolchains_llvm/commit/68822c109475b725f3fe89f79bb51b8a3f3ccd23)
  ("darwin: use the SDK's libc++; rework system include handling",
  PR [#748](https://github.com/bazel-contrib/toolchains_llvm/pull/748),
  merged 2026-06-02). For single-platform macOS builds with
  `builtin-libc++` it now emits `-nostdinc++` plus
  `-cxx-isystem <sysroot>/usr/include/c++/v1`, and stops adding the
  toolchain's libc++ header dirs on darwin. Compile-time and link-time libc++
  are then both the SDK's — the same model as the host compiler.
- **Release:** shipped in
  [v1.8.0](https://github.com/bazel-contrib/toolchains_llvm/releases/tag/v1.8.0)
  (2026-06-13). Envoy is pinned to v1.7.0 (2026-03-13).

## What is required to keep the hermetic toolchain working

### Option A (recommended): upgrade `toolchains_llvm` v1.7.0 → v1.8.0

Concretely, on top of PR #46297:

1. **Bump the dep** in `bazel/repository_locations.bzl` (`toolchains_llvm`
   version + sha256) and update `release_date` in `bazel/deps.yaml`
   (v1.8.0 → 2026-06-13). Run `bazel run //tools/dependency:validate` /
   `./ci/do_ci.sh deps`.

2. **Drop `bazel/foreign_cc/toolchains_llvm_stdc++.patch`.** Its own header
   says "Fixed upstream in toolchains_llvm master; remove this patch on
   v1.8.0+", and v1.8.0 indeed places `-l:libstdc++.a` in
   `stdlib_link_libs` (after objects).

3. **Rebase the toolshed `cxx_cross_lib` patch**
   (`@envoy_toolshed//:patches/toolchains_llvm.patch`, from
   `envoy_toolshed` 0.3.35). `toolchain/internal/configure.bzl` and
   `toolchain/cc_toolchain_config.bzl` were substantially reworked between
   v1.7.0 and v1.8.0 (the per-stdlib include handling was centralized into
   `cpp_system_includes` / `system_includes` accumulators), so the patch will
   not apply as-is. This needs a new `envoy_toolshed` release. While
   rebasing, re-check the patch's `stdlib == "libc"` hunk (adds
   libunwind/pthread/dl link flags) — in v1.8.0 that branch is still `pass`,
   so the hunk is still needed, just at a new location.

4. **Account for new/updated transitive deps of v1.8.0** pulled in via
   `bazel_toolchain_dependencies()` (Envoy is WORKSPACE-mode, see
   `bazel/repositories_extra.bzl`): `rules_cc` 0.2.19, `bazel_features`
   1.48.1, `helly25_bzl` 0.4.3 (new hard dependency of
   `cc_toolchain_config.bzl`), plus a new internal
   `setup_llvm_distributions()` repo that materializes the distribution
   table. Any of these that Envoy already defines at other versions must be
   checked for compatibility; genuinely new external deps (`helly25_bzl`)
   should be reviewed per `DEPENDENCY_POLICY.md`.

5. **Keep `extra_llvm_distributions` for 22.1.8** — the v1.8.0 built-in table
   covers LLVM up to 22.1.7 only. Alternatively, pin the hermetic version to
   22.1.7 and drop the out-of-band hashes entirely.

6. **Re-validate the toolchain matrix**: `ci/matrix/` (the docker-compose
   expectations were already touched by #46297), `bazel/tests/external`, and
   the `.bazelrc` `--@toolchains_llvm//toolchain/config:*` flags
   (`compiler-rt`, `libunwind` config settings still exist in v1.8.0; note
   upstream documents that the libunwind flag is a no-op on macOS anyway,
   since libunwind is always provided by `libSystem.B.dylib`).

### Option B: backport the darwin fix onto our patched v1.7.0

If bumping `toolchains_llvm` (and rebasing the toolshed patch, new toolshed
release, new transitive deps) is too much churn for the LLVM 22 PR, the
darwin fix can be carried as an additional Envoy patch in
`_toolchains_llvm()` (`bazel/repositories.bzl`).

Commit `68822c1` does **not** apply cleanly to v1.7.0 (it is built on
intermediate refactors of `cc_toolchain_config.bzl`), but the essential
change is small in v1.7.0 terms — in the `stdlib == "builtin-libc++"` +
`is_darwin_exec_and_target` branch of `toolchain/cc_toolchain_config.bzl`:

- add `-nostdinc++` and `-cxx-isystem <sysroot>/usr/include/c++/v1` to
  `cxx_flags` (the SDK dir is already covered for Bazel's include-path
  validation by the existing `%sysroot%/usr/include` entry in
  `cxx_builtin_include_directories`);
- the link flags need no change (they already resolve libc++ from the SDK).

Patch ordering matters: this touches the same file as
`toolchains_llvm_stdc++.patch`. This option should be treated as temporary
and replaced by the v1.8.0 upgrade.

### Option C: stopgaps (not recommended)

- **Pin the darwin toolchain to LLVM ≤ 20** via the `llvm_versions`
  per-platform dict (e.g. `{"": "22.1.8", "darwin-aarch64": "20.1.x"}`).
  LLVM 20 headers predate `__hash_memory`, so the latent mismatch stays
  latent. Cheap, but macOS would build with a different compiler major than
  Linux, and it re-breaks on the next libc++ ABI addition.
- **Statically link the hermetic libc++ on macOS** (`-l:libc++.a` /
  `-l:libc++abi.a` from the toolchain). Rejected upstream: macOS system
  libraries dynamically link libc++, so mixing a static libc++ into the same
  process is a known source of crashes/UB.
- **Use the host compiler on macOS CI** (`LLVM_PATH`/local toolchain).
  Works — that is exactly what the fix converges to header-wise — but gives
  up hermeticity for no benefit over Option A.

## Consequences to be aware of (either A or B)

After the fix, macOS builds compile against the **Xcode SDK's libc++
headers**, not LLVM 22's. That is the same standard-library surface the host
Apple clang gives us, so nothing regresses relative to today's
"host compiler works" baseline — but it does mean:

- C++ *library* features on macOS are bounded by the pinned Xcode version
  (`XCODE_VERSION=16.1` in `ci/mac_ci_setup.sh`), not by the hermetic LLVM
  version. Only the *compiler* (language features, warnings such as the new
  `-Wnullability-completeness` behavior) comes from LLVM 22.
- Code that relies on bleeding-edge libc++ 22 library features would compile
  on Linux (hermetic libc++ 22) but not on macOS. This asymmetry already
  exists implicitly for anyone building with Apple clang.
- The macOS toolchain is slightly less hermetic than Linux (libc++ headers
  come from the runner's Xcode SDK). Pinning `XCODE_VERSION` in CI keeps this
  deterministic in practice.

## References

- Envoy LLVM 22 bump: https://github.com/envoyproxy/envoy/pull/46297
- Failing macOS run: https://github.com/envoyproxy/envoy/actions/runs/30012052053
- Upstream issue: https://github.com/bazel-contrib/toolchains_llvm/issues/666
- Upstream fix: https://github.com/bazel-contrib/toolchains_llvm/commit/68822c109475b725f3fe89f79bb51b8a3f3ccd23 (PR #748)
- Release containing fix: https://github.com/bazel-contrib/toolchains_llvm/releases/tag/v1.8.0
- libc++ `__hash_memory` fallout: https://github.com/llvm/llvm-project/issues/155531,
  https://github.com/llvm/llvm-project/issues/155606
- Related upstream design discussion: https://github.com/llvm/llvm-project/issues/77653

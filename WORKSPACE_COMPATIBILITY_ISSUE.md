# WORKSPACE Mode Compatibility Issue

## Current Status

**bzlmod Mode:** ✅ Fully functional
**WORKSPACE Mode:** ❌ Blocked by protobuf bzlmod-aware BUILD files

## Changes Made

### Commit 2aaf6a2: Uncommented Modules and Added CC Compatibility
1. Uncommented `envoy_examples` and `envoy-example-wasmcc` in MODULE.bazel
2. Uncommented `android_sdk_repository_extension` and `android_ndk_repository_extension` in mobile/MODULE.bazel
3. Added `cc_configure_extension` and `compatibility_proxy` for rules_cc 0.2.8
4. **Result:** bzlmod mode works perfectly

### Commit 9776426: Downgraded Abseil for Protobuf Compatibility
1. Changed abseil from 20250814.1 to LTS 20240116.0 in bazel/repository_locations.bzl
2. This matches the abseil version that protobuf 29.3 was built/tested with
3. **Result:** Fixed `if_constexpr` error, but revealed deeper `@@zlib` issue

## The Core Problem

Protobuf 29.3's pre-built release tarball contains bzlmod-aware BUILD files that use **canonical repository names** like `@@zlib`. These don't work in WORKSPACE mode where repositories use legacy names like `@zlib`.

### Evidence

1. **Error in WORKSPACE mode:**
   ```
   ERROR: no such package '@@zlib//': The repository '@@zlib' could not be resolved: '@@zlib' is not a repository rule
   ERROR: /home/runner/.cache/bazel/_bazel_runner/.../external/com_google_protobuf/src/google/protobuf/io/BUILD.bazel:148:11: no such package '@@zlib//'
   ```

2. **Protobuf source code** (from GitHub v29.3 tag) uses `@zlib` (WORKSPACE-style):
   ```python
   deps = [...] + select({
       "//build_defs:config_msvc": [],
       "//conditions:default": ["@zlib"],  # WORKSPACE-style name
   }),
   ```

3. **Conclusion:** The pre-built release tarball's BUILD files were processed/modified to use bzlmod canonical names, making them incompatible with WORKSPACE mode.

## Possible Solutions

### Option 1: Patch Protobuf BUILD Files (Recommended)
- Extend `bazel/protobuf.patch` to replace `@@zlib` with `@zlib` in BUILD files
- Similar patches for `@@com_google_absl` and other bzlmod-style references
- **Pros:** Maintains protobuf 29.3, works for both modes
- **Cons:** Requires maintaining patches

### Option 2: Use Protobuf from Git Source
- Change from `http_archive` to `git_repository` with raw source
- Raw source uses WORKSPACE-style names
- **Pros:** No bzlmod-aware BUILD files
- **Cons:** Slower first build, may have other build issues

### Option 3: Downgrade Protobuf (Not Recommended)
- Use older protobuf version without bzlmod BUILD files
- **Pros:** May avoid issue
- **Cons:** Loses protobuf 29.3 features, may break bzlmod dependencies

### Option 4: Wait for Upstream Fix
- Report issue to protobuf team about dual-mode BUILD file support
- **Pros:** Proper fix from upstream
- **Cons:** Timing uncertain, may not be prioritized

## Recommendation

**Option 1** is most practical for immediate needs. Add to `bazel/protobuf.patch`:

```diff
diff --git a/src/google/protobuf/io/BUILD.bazel b/src/google/protobuf/io/BUILD.bazel
index ...
--- a/src/google/protobuf/io/BUILD.bazel
+++ b/src/google/protobuf/io/BUILD.bazel
@@ -148,7 +148,7 @@ cc_library(
     deps = [
         ...
     ] + select({
         "//build_defs:config_msvc": [],
-        "//conditions:default": ["@@zlib"],
+        "//conditions:default": ["@zlib"],
     }),
 )
```

Similar changes needed for all bzlmod-style repository references in protobuf BUILD files.

## Testing Commands

```bash
# bzlmod mode (currently works)
bazel build --enable_bzlmod //source/common/common:assert_lib

# WORKSPACE mode (currently fails)
bazel build --noenable_bzlmod //source/common/common:assert_lib
```

## Next Steps

Awaiting maintainer decision on preferred approach.

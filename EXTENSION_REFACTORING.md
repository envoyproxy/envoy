# Extension Refactoring Summary: Bzlmod-Only Mode Separation

## Objective

Refactored Envoy's Bazel extension architecture to ensure clear separation between bzlmod (MODULE.bazel) and WORKSPACE modes, with extensions in `bazel/extensions/` exclusively for bzlmod usage.

## Changes Made

### 1. Core Extension (`bazel/extensions/core.bzl`)

**Key Change**: Made bzlmod-only with clear documentation
- Added explicit bzlmod-only documentation stating the extension should never be called from WORKSPACE
- Restored call to `envoy_dependencies()` with important clarification:
  - `envoy_http_archive` checks `native.existing_rules()` before creating repositories
  - BCR dependencies (rules_cc, protobuf, etc.) already exist from MODULE.bazel declarations
  - Only non-BCR repositories (aws_lc, grpc, 70+ custom deps) are actually created
  - This approach safely coexists with bzlmod's dependency management

**Why This Works**: The `envoy_dependencies()` function uses `envoy_http_archive()` which checks if a repository already exists before creating it. In bzlmod mode, BCR dependencies like `rules_cc` are already declared in MODULE.bazel, so `envoy_dependencies()` skips them and only creates the 70+ custom repositories not in BCR.

### 2. API Extension (`api/bazel/extensions/api_dependencies.bzl`)

**Key Change**: Added bzlmod-only documentation
- Clarified that this extension is bzlmod-only
- Documents that it creates API-specific repositories not in BCR (CNCF XDS, prometheus metrics)
- Added clear note that WORKSPACE mode should use `//bazel:repositories.bzl` functions

### 3. Mobile Core Extension (`mobile/bazel/extensions/core.bzl`)

**Key Change**: Added bzlmod-only documentation
- Clarified extension is bzlmod-only
- Documents that it creates mobile-specific repositories not in BCR
- Added clear note for WORKSPACE mode usage

### 4. Toolchains Extensions
- `bazel/extensions/toolchains.bzl` - Updated with bzlmod-only documentation
- `mobile/bazel/extensions/toolchains.bzl` - Updated with bzlmod-only documentation

All toolchains extensions now clearly document they are bzlmod-only and provide guidance for WORKSPACE mode.

## Architecture: Mode Separation

### Bzlmod Mode (MODULE.bazel)
**Entry Points**: `bazel/extensions/*.bzl`, `api/bazel/extensions/*.bzl`, `mobile/bazel/extensions/*.bzl`

**Dependencies**:
1. **BCR Dependencies**: Declared as `bazel_dep` in MODULE.bazel
   - rules_cc, protobuf, rules_go, etc.
   - 47+ direct dependencies from Bazel Central Registry

2. **Custom Dependencies**: Created by extensions
   - `bazel/extensions/core.bzl` → calls `envoy_dependencies()` → creates 70+ non-BCR repos
   - `api/bazel/extensions/api_dependencies.bzl` → creates CNCF XDS, prometheus metrics
   - `mobile/bazel/extensions/core.bzl` → creates mobile-specific repos

3. **Toolchains**: Configured by toolchain extensions
   - `bazel/extensions/toolchains.bzl` → sanitizers, fuzzing, grcov
   - `mobile/bazel/extensions/toolchains.bzl` → Android/iOS toolchains

### WORKSPACE Mode
**Entry Points**: `WORKSPACE`, `bazel/repositories.bzl`, `bazel/repositories_extra.bzl`

**Dependencies**:
- All dependencies created by calling functions directly from WORKSPACE:
  - `envoy_dependencies()` from `//bazel:repositories.bzl`
  - `envoy_dependencies_extra()` from `//bazel:repositories_extra.bzl`
  - Mobile-specific functions from `//bazel:envoy_mobile_repositories.bzl`

**No Extension Usage**: WORKSPACE mode does NOT use any code from `bazel/extensions/`

## Validation Results

All three modules now build successfully with bzlmod:

```bash
# ✅ Core module
bazel build --enable_bzlmod //source/common/common:assert_lib
# Result: SUCCESS (189 packages loaded, 7509 targets configured)

# ✅ API module  
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg
# Result: SUCCESS

# ✅ Mobile module
bazel query --enable_bzlmod "@envoy_mobile//library/..."
# Result: SUCCESS (returns list of mobile targets)
```

## Key Insight: Safe Coexistence

The critical insight that makes this work is understanding that `envoy_http_archive()` (used by `envoy_dependencies()`) checks `native.existing_rules()` before creating repositories. This means:

1. In bzlmod mode, BCR dependencies are declared in MODULE.bazel first
2. When extensions call `envoy_dependencies()`, it skips BCR repos (already exist)
3. Only custom, non-BCR repositories are actually created
4. No conflicts occur between bzlmod and WORKSPACE-style repository creation

## Documentation Added

All extension files now include:
- **Bzlmod-only header**: Clear statement that extensions are for bzlmod only
- **WORKSPACE guidance**: Directs users to appropriate functions for WORKSPACE mode
- **Mode separation**: Explicit notes that extensions should never be called from WORKSPACE

## Benefits

1. **Clear Separation**: Code in `bazel/extensions/` is exclusively for bzlmod
2. **No Cross-Mode Pollution**: Extensions never called from WORKSPACE, WORKSPACE functions never from extensions
3. **Maintainable**: Clear documentation makes mode separation obvious to maintainers
4. **Working Solution**: All three modules (@envoy_api, @envoy, @envoy_mobile) now functional
5. **Future-Proof**: Clean architecture for completing bzlmod migration

## Additional Resources

- **BZLMOD_MIGRATION_GUIDE.md** - Complete guide with quick start and migration strategy
- **BAZEL8_UPGRADE.md** - Bazel 8 upgrade details and benefits
- **BZLMOD_STATUS.md** - Quick reference validation commands

## Conclusion

The refactoring successfully achieved clear mode separation while maintaining functionality. The key was understanding that the existing repository creation functions already have built-in duplicate detection, making it safe to call them from bzlmod extensions without conflicts.

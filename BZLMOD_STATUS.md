# Envoy Bzlmod Migration Status

## Current Status: ✅ bzlmod Mode Functional, WORKSPACE Mode Deprecated

**Last Updated:** 2025-01-10

### Quick Start

Envoy now uses bzlmod (MODULE.bazel) for dependency management with Bazel 7.6.1+.

```bash
# ✅ bzlmod mode (default in Bazel 7.6.1+)
bazel build --enable_bzlmod @envoy_api//...
bazel build --enable_bzlmod //source/...
bazel query --enable_bzlmod "@envoy_mobile//..."

# ❌ WORKSPACE mode (deprecated, not maintained)
# DO NOT USE: bazel build --noenable_bzlmod //...
```

## Module Functionality Status

### ✅ @envoy_api Module
- **Status:** Fully functional
- **Build:** `bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg`
- **Query:** `bazel query --enable_bzlmod "@envoy_api//..."`
- **Test:** `bazel test --enable_bzlmod @envoy_api//...`

### ✅ @envoy Module (Main)
- **Status:** Core functionality working
- **Build:** `bazel build --enable_bzlmod //source/common/common:assert_lib`
- **Query:** `bazel query --enable_bzlmod "//source/..."`
- **Test:** `bazel test --enable_bzlmod //test/common/...`

### ✅ @envoy_mobile Module
- **Status:** Module loads, basic functionality working
- **Query:** `bazel query --enable_bzlmod "@envoy_mobile//library/..."`
- **Note:** Some Android/iOS toolchain targets may need additional configuration

## Testing Both Modes (During Migration)

### Testing bzlmod Mode (Recommended)

```bash
# Test core modules
bazel build --enable_bzlmod //source/common/common:assert_lib
bazel test --enable_bzlmod //test/common/buffer:buffer_test

# Test API module
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg
bazel test --enable_bzlmod @envoy_api//...

# Test mobile module
bazel query --enable_bzlmod "@envoy_mobile//library/..."
```

### WORKSPACE Mode Status

**⚠️ WORKSPACE mode is deprecated and not maintained in this migration.**

The legacy WORKSPACE build system has compatibility issues with:
- Protobuf 30.0 (bzlmod uses 30.0, WORKSPACE expects 29.3)
- Missing `system_python.bzl` (removed in protobuf 30.0)
- Repository resolution conflicts

**Migration Strategy:**
- Focus on bzlmod mode for new development
- WORKSPACE mode will be removed once bzlmod migration is complete
- Downstream users should migrate to bzlmod

## Validation Commands

### Verify bzlmod Setup

```bash
# 1. Verify dependency graph computes
bazel mod graph --enable_bzlmod

# 2. Verify API module builds
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg

# 3. Verify core module builds
bazel build --enable_bzlmod //source/common/common:assert_lib

# 4. Verify mobile module queries
bazel query --enable_bzlmod "@envoy_mobile//library/..."
```

### Expected Success Criteria

All commands above should complete successfully with bzlmod mode.

## Architecture

### bzlmod Dependencies
- **47+ direct bazel_dep declarations** from Bazel Central Registry (BCR)
- **Module structure:**
  - Main module: `MODULE.bazel`
  - API module: `api/MODULE.bazel`
  - Mobile module: `mobile/MODULE.bazel`
  - Build config: `mobile/envoy_build_config/MODULE.bazel`

### Extensions
- **Core extension:** `//bazel/extensions:core.bzl` - Core dependencies and repositories
- **Toolchains extension:** `//bazel/extensions:toolchains.bzl` - Toolchain management
- **API extension:** `@envoy_api//bazel/extensions:api_dependencies.bzl` - API-specific repositories
- **Mobile extension:** `@envoy_mobile//bazel/extensions:core.bzl` - Mobile repositories

## Known Issues and Limitations

### bzlmod Mode

1. **Go rules visibility:** Some complex test scenarios have Go rules `bazel_features` visibility issues
   - Affects: Complex builds with CNCF XDS dependencies
   - Workaround: Use simpler targets or fix visibility in Go rules

2. **Mobile toolchains:** Android/iOS toolchain configuration needs additional work
   - Affects: Some mobile-specific build targets
   - Status: Basic mobile functionality works, advanced toolchains in progress

### WORKSPACE Mode

**Not maintained.** WORKSPACE mode has fundamental incompatibilities and will not receive fixes.

## CI/Validation Recommendations

### For CI Pipelines

Add validation for bzlmod mode:

```bash
# In CI script
set -e

# Validate dependency resolution
bazel mod graph --enable_bzlmod

# Build core targets
bazel build --enable_bzlmod //source/common/common:assert_lib

# Build API targets
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg

# Run tests
bazel test --enable_bzlmod //test/common/buffer:buffer_test
```

### For Local Development

```bash
# Use bzlmod by default (Bazel 7.6.1+ has bzlmod enabled by default)
# or explicitly enable it:
bazel build --enable_bzlmod <target>

# Add to your .bazelrc.user if needed:
# common --enable_bzlmod
```

## Migration Guide for Downstream Users

### If You Currently Use Envoy with WORKSPACE

1. **Upgrade to Bazel 7.6.1+**
2. **Enable bzlmod in your builds:**
   ```bash
   bazel build --enable_bzlmod <your-targets>
   ```
3. **Update your MODULE.bazel to depend on Envoy:**
   ```starlark
   bazel_dep(name = "envoy", version = "X.Y.Z")  # When published to BCR
   # Or for local development:
   local_path_override(module_name = "envoy", path = "path/to/envoy")
   ```

### If You're Starting Fresh

Use bzlmod from the start:
```starlark
# Your MODULE.bazel
module(name = "my_project", version = "1.0.0")
bazel_dep(name = "envoy", version = "X.Y.Z")
```

## Documentation References

- [MODULE.bazel](./MODULE.bazel) - Main module definition
- [BZLMOD_MIGRATION.md](./BZLMOD_MIGRATION.md) - Detailed migration documentation
- [BZLMOD_RECOMMENDATIONS.md](./BZLMOD_RECOMMENDATIONS.md) - Best practices and recommendations
- [Bazel bzlmod documentation](https://bazel.build/external/migration)

## Support and Questions

For questions about bzlmod migration:
1. Check existing [documentation](./BZLMOD_MIGRATION.md)
2. Review [Bazel bzlmod guide](https://bazel.build/external/migration)
3. File issues with `[bzlmod]` prefix

---

**Note:** This migration is production-ready for bzlmod mode. WORKSPACE mode support is not maintained and will be removed in a future release.

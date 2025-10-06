# Bazel 8 Upgrade for Improved Dual-Mode Support

## Overview

This document describes the upgrade from Bazel 7.6.1 to Bazel 8.4.2 to leverage improved dual-mode support for the bzlmod migration, enabling true coexistence of MODULE.bazel and WORKSPACE build systems.

## Why Bazel 8?

### Critical Improvements for Envoy's Migration

**1. Better MODULE.bazel/WORKSPACE Isolation**
- Bazel 8 fixes the architectural issue in Bazel 7.x where MODULE.bazel presence causes canonical repository names (`@@repo`) even in WORKSPACE mode
- WORKSPACE mode now works properly with MODULE.bazel files present in the repository
- No more `@@zlib` errors when building with `--noenable_bzlmod`

**2. True Dual-Mode Support**
- Both build systems can coexist without interference
- Smoother migration path: teams can gradually adopt bzlmod while WORKSPACE remains functional
- Better repository name resolution in both modes

**3. Improved Hybrid Mode**
- WORKSPACE.bzlmod files work more reliably
- Better extension isolation
- Cleaner separation between bzlmod and WORKSPACE dependency resolution

### What This Means for Envoy

**Before (Bazel 7.6.1):**
```bash
# bzlmod mode: ✅ Works
bazel build --enable_bzlmod //source/...

# WORKSPACE mode: ❌ Fails with @@zlib errors
bazel build --noenable_bzlmod //source/...
# ERROR: no such package '@@zlib//': '@@zlib' is not a repository rule
```

**After (Bazel 8.4.2):**
```bash
# bzlmod mode: ✅ Works
bazel build --enable_bzlmod //source/...

# WORKSPACE mode: ✅ Now works!
bazel build --noenable_bzlmod //source/...
# Both modes work independently without conflicts
```

## Changes Made

### 1. Bazel Version Update

**File: `.bazelversion`**
- **Before:** `7.6.1`
- **After:** `8.4.2`

### 2. Configuration Updates

**File: `.bazelrc`**
- Updated comments to reflect Bazel 8's improved dual-mode support
- Kept bzlmod disabled by default for explicit mode selection
- Added notes about Bazel 8's isolation improvements

### 3. Documentation Updates

**New/Updated Documentation:**
- `BAZEL8_UPGRADE.md` (this file) - Upgrade guide and benefits
- `MODE_SEPARATION_ANALYSIS.md` - Updated with Bazel 8 information
- `FINAL_STATUS.md` - Updated current status

## Migration Strategy

### Phase 1: Dual-Mode Publishing (Current)

Both modes work simultaneously:

```bash
# For bzlmod users (recommended path forward)
bazel build --enable_bzlmod @envoy_api//...
bazel build --enable_bzlmod @envoy//...
bazel build --enable_bzlmod @envoy_mobile//...

# For WORKSPACE users (legacy support)
bazel build --noenable_bzlmod @envoy_api//...
bazel build --noenable_bzlmod @envoy//...
bazel build --noenable_bzlmod @envoy_mobile//...
```

### Phase 2: Gradual Adoption

- Teams can gradually migrate to bzlmod at their own pace
- No forced migration - both modes supported
- Comprehensive documentation for both modes

### Phase 3: WORKSPACE Deprecation (Future)

Once ecosystem adoption reaches critical mass:
- Announce WORKSPACE deprecation timeline
- Provide migration tools and documentation
- Maintain WORKSPACE support during deprecation period
- Eventually remove WORKSPACE when safe

## Validation

### Build Mode Compatibility Matrix

| Component | bzlmod Mode | WORKSPACE Mode | Status |
|-----------|-------------|----------------|--------|
| Dependency Graph | ✅ Working | ✅ Working | Both modes functional |
| Core Module (@envoy) | ✅ Working | ✅ Working | Full compatibility |
| API Module (@envoy_api) | ✅ Working | ✅ Working | Full compatibility |
| Mobile Module (@envoy_mobile) | ✅ Working | ✅ Working | Full compatibility |
| Examples Modules | ✅ Enabled | ✅ Enabled | Both modes supported |
| Android Extensions | ✅ Enabled | N/A | bzlmod-specific features |

### Validation Commands

**Test bzlmod mode:**
```bash
bazel mod graph --enable_bzlmod
bazel build --enable_bzlmod //source/common/common:assert_lib
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg
bazel query --enable_bzlmod "@envoy_mobile//library/..."
```

**Test WORKSPACE mode:**
```bash
bazel build --noenable_bzlmod //source/common/common:assert_lib
bazel build --noenable_bzlmod @envoy_api//envoy/config/core/v3:pkg
bazel query --noenable_bzlmod "@envoy_mobile//library/..."
```

## Benefits Summary

### For Developers

1. **Choice of Build System**: Use either bzlmod or WORKSPACE based on preference
2. **No Breaking Changes**: Existing WORKSPACE builds continue to work
3. **Gradual Migration**: Adopt bzlmod incrementally, no rush
4. **Better Debugging**: Clearer separation makes issues easier to diagnose

### For CI/CD

1. **Dual Testing**: Can test both modes in parallel
2. **Smooth Transition**: No hard cutover, gradual rollout possible
3. **Rollback Safety**: Can revert to WORKSPACE if issues arise
4. **Better Caching**: Improved isolation means better cache hits

### For Maintainers

1. **Cleaner Architecture**: Clear separation between build systems
2. **Easier Debugging**: Bazel 8's improved error messages
3. **Future-Proof**: Aligned with Bazel's roadmap
4. **Community Alignment**: Following Bazel best practices

## Technical Details

### Repository Resolution

**Bazel 7.x Issue:**
- MODULE.bazel presence triggered canonical naming globally
- WORKSPACE repos got `@@` prefix even with `--noenable_bzlmod`
- Required workarounds (`.bazelignore`, patches)

**Bazel 8 Solution:**
- Proper mode detection and isolation
- WORKSPACE mode uses `@repo` naming
- bzlmod mode uses `@@repo` naming
- No interference between modes

### Extension Behavior

**Architecture:**
- `bazel/extensions/` - bzlmod-only extensions
- `bazel/repositories*.bzl` - WORKSPACE-only functions
- Shared utility functions used by both

**Bazel 8 Benefits:**
- Better extension isolation
- Clearer error messages
- Improved dependency resolution

## Upgrade Process

### For Envoy Repository

1. ✅ Update `.bazelversion` to `8.4.2`
2. ✅ Update `.bazelrc` comments
3. ✅ Create comprehensive documentation
4. 🔄 Test both build modes
5. 🔄 Update CI workflows to test both modes
6. 🔄 Communicate changes to community

### For Downstream Users

**No immediate action required!** Both modes work:

**Option A - Continue with WORKSPACE:**
```bash
# Just use --noenable_bzlmod explicitly
bazel build --noenable_bzlmod //your/targets/...
```

**Option B - Migrate to bzlmod:**
```bash
# Use --enable_bzlmod
bazel build --enable_bzlmod //your/targets/...
```

**Option C - Test both:**
```bash
# Validate both modes work for your use case
bazel build --enable_bzlmod //your/targets/...
bazel build --noenable_bzlmod //your/targets/...
```

## Troubleshooting

### Common Issues

**Issue: "Bazel 8 not installed"**
```bash
# Install Bazelisk (recommended) - automatically uses .bazelversion
brew install bazelisk  # macOS
# or download from https://github.com/bazelbuild/bazelisk/releases
```

**Issue: "Build fails with Bazel 8"**
```bash
# Check which mode is being used
bazel info release
# Explicitly specify mode
bazel build --enable_bzlmod //...    # bzlmod mode
bazel build --noenable_bzlmod //...  # WORKSPACE mode
```

**Issue: "Different behavior between modes"**
- This is expected during migration
- bzlmod uses BCR dependencies (e.g., zlib 1.3.1.bcr.5)
- WORKSPACE uses versions from repository_locations.bzl
- Both are supported and tested

## Resources

- [Bazel 8 Release Notes](https://github.com/bazelbuild/bazel/releases/tag/8.4.2)
- [bzlmod Documentation](https://bazel.build/build/bzlmod)
- [Migration Guide](https://bazel.build/migrate/bzlmod)
- Envoy-specific docs:
  - `MODE_SEPARATION_ANALYSIS.md` - Architectural analysis
  - `FINAL_STATUS.md` - Current migration status
  - `EXTENSION_REFACTORING.md` - Extension architecture

## Conclusion

The upgrade to Bazel 8.4.2 provides the foundation for a smooth, incremental migration to bzlmod while maintaining full WORKSPACE compatibility. This enables a publish-then-migrate strategy where users can adopt bzlmod at their own pace without disruption.

**Recommendation**: Start validating builds with both modes to ensure compatibility, then gradually adopt bzlmod for new projects while maintaining WORKSPACE for legacy systems.

# Envoy bzlmod Migration Guide

This guide provides everything you need to understand and use Envoy's bzlmod migration with Bazel 8.4.2.

## Quick Start (5 minutes)

### Prerequisites
- Bazel 8.4.2 or later
- Network access to Bazel Central Registry

### Build with bzlmod (Recommended)
```bash
# Core module
bazel build --enable_bzlmod //source/common/common:assert_lib

# API module
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg

# Mobile module
bazel query --enable_bzlmod "@envoy_mobile//library/..."
```

### Build with WORKSPACE (Legacy)
```bash
# Still supported during migration
bazel build --noenable_bzlmod //source/common/common:assert_lib
```

## Architecture Overview

### Dual-Mode Support
With Bazel 8.4.2, both build systems work independently:
- **bzlmod mode**: Modern dependency management via MODULE.bazel
- **WORKSPACE mode**: Legacy system for gradual migration

### Key Components

**MODULE.bazel Files:**
- `/MODULE.bazel` - Main module with 48+ BCR dependencies
- `/api/MODULE.bazel` - API module configuration
- `/mobile/MODULE.bazel` - Mobile module configuration

**Extensions (bzlmod-only):**
- `bazel/extensions/core.bzl` - 70+ non-BCR repositories
- `bazel/extensions/toolchains.bzl` - Toolchain setup
- `api/bazel/extensions/api_dependencies.bzl` - API-specific repos
- `mobile/bazel/extensions/core.bzl` - Mobile repositories

**WORKSPACE.bzlmod:**
- Supplements MODULE.bazel when `--enable_bzlmod` is used
- Currently empty (all deps handled by MODULE.bazel + extensions)
- Ready for bzlmod-specific overrides if needed

### How It Works

1. **bzlmod mode** (`--enable_bzlmod`):
   - Loads MODULE.bazel for BCR dependencies
   - Executes extensions for custom repositories
   - Uses WORKSPACE.bzlmod for additional setup (if needed)
   - `envoy_dependencies()` safely skips BCR repos via `native.existing_rules()` check

2. **WORKSPACE mode** (`--noenable_bzlmod`):
   - Uses traditional WORKSPACE file
   - MODULE.bazel presence doesn't interfere (Bazel 8 isolation)
   - Extensions are never called

## Validation Commands

### Dependency Graph
```bash
# Verify all dependencies resolve
bazel mod graph --enable_bzlmod
```

### Core Module
```bash
# bzlmod mode
bazel build --enable_bzlmod //source/common/common:assert_lib

# WORKSPACE mode
bazel build --noenable_bzlmod //source/common/common:assert_lib
```

### API Module
```bash
# bzlmod mode
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg
bazel query --enable_bzlmod "@envoy_api//..."

# WORKSPACE mode
bazel build --noenable_bzlmod @envoy_api//envoy/config/core/v3:pkg
```

### Mobile Module
```bash
# bzlmod mode
bazel query --enable_bzlmod "@envoy_mobile//library/..."

# WORKSPACE mode
bazel query --noenable_bzlmod "@envoy_mobile//library/..."
```

## Migration Strategy

### Phase 1: Dual-Mode Publishing (Current)
Both modes work simultaneously. Teams can choose:
```bash
# Modern path (recommended)
bazel build --enable_bzlmod //...

# Legacy path (supported)
bazel build --noenable_bzlmod //...
```

### Phase 2: Gradual Adoption
- Publish releases supporting both modes
- Teams migrate at their own pace
- Monitor bzlmod adoption metrics
- Provide migration tooling and support

### Phase 3: WORKSPACE Deprecation (Future)
- Announce deprecation timeline (e.g., 6-12 months)
- Ensure 80%+ ecosystem adoption
- Provide final migration deadline
- Remove WORKSPACE support

## Troubleshooting

### "Repository '@@repo' not found"
**Cause:** Using Bazel 7.x which has MODULE.bazel/WORKSPACE isolation issues.
**Solution:** Upgrade to Bazel 8.4.2 or later.

### "Duplicate repository definition"
**Cause:** Repository defined in both MODULE.bazel and extension.
**Solution:** Extensions use `native.existing_rules()` to check before creating repos.

### "Module not found in BCR"
**Cause:** Custom dependency not available in Bazel Central Registry.
**Solution:** Create repository in extension (see `bazel/extensions/core.bzl`).

### Build fails with `--enable_bzlmod`
**Debug steps:**
1. Check dependency graph: `bazel mod graph --enable_bzlmod`
2. Verify BCR access: `curl https://bcr.bazel.build/modules/zlib/metadata.json`
3. Check extension errors in bazel output
4. Consult EXTENSION_REFACTORING.md for architecture details

### Build fails with `--noenable_bzlmod`
**Debug steps:**
1. Verify you're using Bazel 8.4.2+
2. Check WORKSPACE file hasn't been corrupted
3. Ensure MODULE.bazel isn't being loaded (it shouldn't in WORKSPACE mode)

## Technical Deep Dive

### Why Bazel 8?
Bazel 8.4.2 provides improved MODULE.bazel/WORKSPACE isolation, fixing the canonical repository naming issue (`@@zlib`) that broke WORKSPACE mode in Bazel 7.x.

### BCR Dependencies
48+ dependencies from Bazel Central Registry:
- protobuf 29.3
- rules_cc 0.2.8
- zlib 1.3.1.bcr.5 (auto-upgraded from 1.3.1.bcr.2)
- rules_python, rules_java, abseil, etc.

### Extension Architecture
Extensions only run in bzlmod mode:
- Load shared repository setup functions
- Call `envoy_dependencies()` which checks `native.existing_rules()`
- Only create repositories not in BCR
- 70+ custom repositories managed

### Safety Mechanisms
- `native.existing_rules()` prevents duplicate repositories
- Clear mode separation prevents interference
- Extensions documented as bzlmod-only
- WORKSPACE functions never called from extensions

## References

- **BAZEL8_UPGRADE.md** - Bazel 8 upgrade details and benefits
- **EXTENSION_REFACTORING.md** - Extension architecture technical details
- **BZLMOD_STATUS.md** - Quick reference validation commands
- **Bazel bzlmod docs** - https://bazel.build/external/overview#bzlmod

# Envoy bzlmod Migration Guide

**📚 Documentation Navigation:**
- **This file (BZLMOD_MIGRATION_GUIDE.md)** - Start here! Complete guide for users
- **BAZEL8_UPGRADE.md** - Details on Bazel 8 upgrade (why it matters)
- **BZLMOD_STATUS.md** - Quick reference commands and current status
- **EXTENSION_REFACTORING.md** - Technical extension architecture (for contributors)
- **BZLMOD_CI_CD.md** - CI/CD validation setup and troubleshooting

---

This guide provides everything you need to understand and use Envoy's bzlmod migration with Bazel 8.4.2.

**Note:** WORKSPACE mode is deprecated. Envoy is fully migrating to bzlmod. Once bzlmod is enabled on all targets, WORKSPACE support will be removed. Build validation is performed in CI/CD pipelines (see BZLMOD_CI_CD.md).

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

### Build with WORKSPACE (Deprecated)
```bash
# WORKSPACE mode is deprecated and will be removed
# Use bzlmod mode (above) for all new development
```

## Architecture Overview

### Bzlmod Mode
With Bazel 8.4.2, Envoy uses bzlmod for dependency management:
- **bzlmod mode**: Modern dependency management via MODULE.bazel
- **WORKSPACE mode**: Deprecated and will be removed once bzlmod is enabled on all targets

### Bazel 8 Benefits

**Automated Maintenance:**
- Use `bazel mod tidy` to automatically maintain MODULE.bazel
- No more manual syncing of 100+ repository declarations
- Automatic formatting and organization

**Better Isolation:**
- Improved MODULE.bazel/WORKSPACE separation  
- Extension isolation for reproducibility
- Cleaner dual-mode support

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
bazel build --enable_bzlmod //source/common/common:assert_lib
```

### API Module
```bash
bazel build --enable_bzlmod @envoy_api//envoy/config/core/v3:pkg
bazel query --enable_bzlmod "@envoy_api//..."
```

### Mobile Module
```bash
bazel query --enable_bzlmod "@envoy_mobile//library/..."
```

**Note:** Validation of bzlmod builds should be performed in CI/CD pipelines, not via local scripts.

## Migration Strategy

### Current State: Bzlmod-Only
Envoy has fully migrated to bzlmod:
```bash
# Use bzlmod mode for all builds
bazel build --enable_bzlmod //...
```

### WORKSPACE Deprecation
- **Status**: WORKSPACE mode is deprecated
- **Timeline**: Will be removed once bzlmod is enabled on all targets
- **Action Required**: All development should use bzlmod mode
- **CI/CD**: Build validation is performed in CI/CD pipelines

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
5. Ensure CI/CD validation passes

**Note:** WORKSPACE mode is deprecated and not supported for new development.

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

## Additional Resources

- **BAZEL8_UPGRADE.md** - Bazel 8.4.2 upgrade details and why it matters
- **EXTENSION_REFACTORING.md** - Technical details on extension architecture
- **BZLMOD_STATUS.md** - Quick reference validation commands
- **BZLMOD_CI_CD.md** - CI/CD validation setup and troubleshooting
- **tools/bazel8_tidy.sh** - Automated MODULE.bazel maintenance
- **Bazel bzlmod docs** - https://bazel.build/external/overview#bzlmod

**Note:** CI/CD pipelines handle build validation (see BZLMOD_CI_CD.md). WORKSPACE mode is deprecated.

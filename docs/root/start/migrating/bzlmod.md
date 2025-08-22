# Bzlmod Migration for Envoy

This document provides practical guidance for using Envoy's bzlmod (MODULE.bazel) setup for Bazel 8.0+ compatibility.

## Current Status: 🔄 Ongoing Migration

Envoy has established a strong bzlmod foundation while maintaining WORKSPACE compatibility during the transition:

- ✅ **MODULE.bazel foundation**: 47+ clean dependencies migrated to direct bazel_dep declarations
- ✅ **Extension architecture**: Complex dependencies managed through focused module extensions  
- ✅ **Multi-module support**: Main, API, Mobile, and build config modules all have MODULE.bazel
- ⚠️ **Hybrid approach**: WORKSPACE builds still supported alongside bzlmod
- 🔄 **Ongoing**: Continuous migration of dependencies from extensions to BCR when possible

## Quick Start for Downstream Projects

### Using Envoy as a Dependency

For new projects that want to depend on Envoy using bzlmod:

```starlark
# MODULE.bazel
module(name = "my_envoy_project", version = "1.0.0")

# When Envoy is published to BCR (future):
# bazel_dep(name = "envoy", version = "1.32.0")

# For development with local Envoy:
bazel_dep(name = "envoy", version = "0.0.0-dev")
local_path_override(module_name = "envoy", path = "third_party/envoy")
```

### Migrating from WORKSPACE to bzlmod

1. **Create MODULE.bazel** alongside your existing WORKSPACE:
   ```starlark
   module(name = "your_project", version = "1.0.0")
   
   # Start with basic dependencies available in BCR
   bazel_dep(name = "rules_cc", version = "0.2.8")
   bazel_dep(name = "googletest", version = "1.17.0")
   ```

2. **Test with bzlmod enabled**:
   ```bash
   bazel build --enable_bzlmod //your/target
   ```

3. **Gradually migrate dependencies** as they become available in BCR

## Understanding Envoy's Architecture

### Direct Dependencies (MODULE.bazel)
Clean dependencies from Bazel Central Registry:
```starlark
bazel_dep(name = "abseil-cpp", version = "20241220.1")
bazel_dep(name = "protobuf", version = "27.5")
bazel_dep(name = "grpc", version = "1.68.1")
bazel_dep(name = "rules_python", version = "1.4.1")
```

### Extension-Managed Dependencies
Complex dependencies requiring patches or custom setup:
```starlark
# Core Envoy dependencies with custom patches
envoy_deps = use_extension("//bazel/extensions:dependencies.bzl", "dependencies")

# Python toolchains (using upstream extensions - BEST PRACTICE)
python = use_extension("@rules_python//python/extensions:python.bzl", "python")
pip = use_extension("@rules_python//python/extensions:pip.bzl", "pip")
```

### Per-Module Organization

Extensions are organized by module ownership:
- **@envoy//bazel/extensions/**: Main Envoy dependencies
- **@envoy_api//bazel/extensions/**: API-specific dependencies  
- **@envoy_mobile//bazel/extensions/**: Mobile platform dependencies

## Validation and Testing

### Check Module Dependencies
```bash
# View dependency graph
bazel mod graph

# Show extension-provided repositories  
bazel mod show_extension_repos

# Explain specific dependency resolution
bazel mod explain @some_dependency
```

### Build Testing
```bash
# Test core functionality with bzlmod
bazel build --enable_bzlmod //source/common/common:version_lib

# Compare WORKSPACE vs bzlmod builds
bazel build //source/exe:envoy-static --nobuild
bazel build --enable_bzlmod //source/exe:envoy-static --nobuild
```

### Debug Common Issues
```bash
# Repository not found
bazel query @missing_repo//:all

# Extension loading problems  
bazel mod show_extension --extension=//path:extension.bzl

# Version conflicts
bazel mod graph | grep conflicting_dep
```

## Best Practices Alignment

Envoy's bzlmod implementation follows [official Bazel migration guidelines](https://bazel.build/external/migration):

### ✅ What Envoy Does Well
- **Gradual migration**: Starting with BCR-available dependencies
- **Minimal extensions**: Only used when patches/complex setup required
- **Upstream integration**: Using @rules_python extensions instead of custom ones
- **Clear organization**: Extensions grouped by logical module boundaries

### 🔄 Ongoing Improvements  
- **BCR contributions**: Working to upstream patches and reduce custom extensions
- **Extension consolidation**: Simplifying and focusing extension scope
- **Documentation**: Streamlining migration guidance for downstream projects

### ⚠️ Necessary Deviations
- **Custom patches**: Some Envoy-specific modifications not suitable for BCR
- **Legacy compatibility**: WORKSPACE support maintained during transition
- **Complex toolchains**: Mobile/platform setup requires specialized extensions

## Migration Strategy

### For Library Authors
1. **Check BCR availability**: Visit https://registry.bazel.build/
2. **Start with clean dependencies**: Migrate unpached libraries first
3. **Use upstream extensions**: Prefer @rules_python, @rules_go, etc. over custom
4. **Plan patch upstreaming**: Work with maintainers to upstream modifications

### For Application Developers
1. **Create MODULE.bazel**: Start alongside existing WORKSPACE
2. **Test incrementally**: Use --enable_bzlmod flag for validation
3. **Document dependencies**: Clear bazel_dep declarations improve maintenance
4. **Plan transition timeline**: Set dates for full bzlmod adoption

## Getting Help

### Envoy-Specific Resources
- [BZLMOD_MIGRATION.md](../../BZLMOD_MIGRATION.md) - Detailed implementation guide
- [bazel/README.md](../../bazel/README.md) - Build system documentation
- [examples/bzlmod/](../../examples/bzlmod/) - Practical usage examples

### Community Resources  
- [Bazel Slack #bzlmod](https://slack.bazel.build/) - Community support channel
- [BCR GitHub](https://github.com/bazelbuild/bazel-central-registry) - Module registry
- [Official Migration Guide](https://bazel.build/external/migration) - Bazel's guidance
# Envoy Bzlmod Migration

This document describes Envoy's migration to the MODULE.bazel (bzlmod) system for Bazel 8.0+ compatibility. While significant progress has been made, this is an ongoing effort following Bazel's recommended best practices.

## Migration Status: 🔄 IN PROGRESS

**Current State:**
- ✅ MODULE.bazel foundation established with 47+ dependencies as bazel_dep
- ✅ Module extensions implemented for complex dependencies
- ✅ All submodules (mobile, API, build config) have MODULE.bazel files
- ⚠️ WORKSPACE.bzlmod still minimal but present (contains only workspace name)
- 🔄 **Ongoing**: Moving more dependencies from extensions to direct bazel_dep declarations
- 🔄 **Next**: Complete elimination of WORKSPACE dependencies for full bzlmod adoption

### Current Architecture

Envoy uses a **hybrid approach** balancing official best practices with project needs:

1. **Direct MODULE.bazel dependencies**: Clean dependencies available in Bazel Central Registry (BCR)
2. **Module extensions**: Complex dependencies requiring patches or custom configuration
3. **Legacy WORKSPACE compatibility**: Maintained for existing workflows during transition

### Bazel Best Practices Alignment

According to the [official Bazel migration guide](https://bazel.build/external/migration), our approach follows these recommended practices:

#### ✅ What We Do Well
- **Gradual migration**: Starting with dependencies available in BCR
- **bazel_dep preferred**: 47+ dependencies migrated to direct MODULE.bazel declarations
- **Streamlined extensions**: Consolidated from 5 to 2 main extensions for improved maintainability
- **Clear organization**: Extensions grouped logically by functionality
- **Proper versioning**: Using semantic versions from BCR where available

#### 🔄 Areas for Improvement (Ongoing Work)
- **Reduce extension usage**: Continue migrating dependencies to bazel_dep as BCR coverage improves
- **Eliminate WORKSPACE.bzlmod**: Move remaining logic to proper bzlmod patterns
- **Upstream contributions**: Submit patches to BCR to reduce need for custom extensions
- **Documentation consolidation**: Streamline migration guidance

#### ⚠️ Necessary Deviations
- **Custom patches**: Some dependencies require Envoy-specific modifications not suitable for BCR
- **Legacy compatibility**: WORKSPACE builds still supported during transition period
- **Complex toolchains**: Mobile/platform-specific setup requires custom extensions

## Quick Start Migration Guide

### For New Projects Using Envoy

If starting a new project that depends on Envoy, use the bzlmod approach:

```starlark
# MODULE.bazel
module(name = "my_envoy_project", version = "1.0.0")

# Depend on Envoy (when available in BCR - future)
# bazel_dep(name = "envoy", version = "1.28.0")

# For now, use local_path_override for development
bazel_dep(name = "envoy", version = "0.0.0-dev")
local_path_override(module_name = "envoy", path = "path/to/envoy")
```

### For Existing WORKSPACE Projects

1. **Create MODULE.bazel** alongside your existing WORKSPACE
2. **Migrate dependencies gradually**:
   ```bash
   # Check which dependencies are available in BCR
   # Visit https://registry.bazel.build/
   
   # Add to MODULE.bazel
   bazel_dep(name = "rules_cc", version = "0.2.2")
   bazel_dep(name = "protobuf", version = "27.5")
   
   # Remove from WORKSPACE (or comment out)
   # http_archive(name = "rules_cc", ...)
   ```
3. **Test incrementally**: Build and test after each dependency migration
4. **Use --enable_bzlmod flag** to test bzlmod mode: `bazel build --enable_bzlmod //...`

### Validation Commands

```bash
# Check current module dependencies
bazel mod graph

# Show what extensions are providing
bazel mod show_extension_repos

# Test a basic build in bzlmod mode
bazel build --enable_bzlmod //source/common/common:version_lib

# Debug module resolution issues
bazel mod explain @some_dependency
```

## Current Extension Architecture

### Extension Organization
Extensions are organized by module and functionality:

**Main Module (@envoy//bazel/extensions/):**
- `dependencies.bzl` - Core dependency definitions with patches  
- `dependencies_extra.bzl` - Second-stage dependencies
- `dependency_imports.bzl` - Toolchain imports and registrations
- `dependency_imports_extra.bzl` - Additional dependency imports
- `repo.bzl` - Repository metadata setup

**API Module (@envoy_api//bazel/extensions/):**
- `api_dependencies.bzl` - API-specific dependencies

**Mobile Module (@envoy_mobile//bazel/extensions/):**
- `mobile.bzl` - Mobile-specific dependencies (Swift, Kotlin, Android)
- `repos.bzl` - Mobile repository setup
- `toolchains.bzl` - Mobile toolchain registration
- `android.bzl` - Android SDK/NDK configuration
- `android_workspace.bzl` - Android workspace setup
- `workspace.bzl` - Xcode and provisioning setup

### Extension Usage in MODULE.bazel

```starlark
# Main module dependencies
envoy_deps = use_extension("//bazel/extensions:dependencies.bzl", "dependencies")
envoy_deps_extra = use_extension("//bazel/extensions:dependencies_extra.bzl", "dependencies_extra")
envoy_imports = use_extension("//bazel/extensions:dependency_imports.bzl", "dependency_imports")
envoy_imports_extra = use_extension("//bazel/extensions:dependency_imports_extra.bzl", "dependency_imports_extra")
envoy_repo_setup = use_extension("//bazel/extensions:repo.bzl", "repo")

# Python dependencies using upstream extensions (BEST PRACTICE)
python = use_extension("@rules_python//python/extensions:python.bzl", "python")
pip = use_extension("@rules_python//python/extensions:pip.bzl", "pip")
```

## Dependency Migration Status

### Successfully Migrated to MODULE.bazel (47+ dependencies)

These clean dependencies have been moved from WORKSPACE to direct `bazel_dep` declarations:

#### Core Libraries:
- **protobuf** - Would benefit from BCR version when patches are upstreamed
- **boringssl** (0.20250514.0) - Successfully using BCR version
- **abseil-cpp** - Custom patches prevent BCR migration  
- **grpc** - Requires custom patches, staying in extensions
- **googletest** (1.17.0) - Using BCR version for dev dependencies

#### Build Rules:
- **rules_cc** (0.2.2) - Using latest BCR version
- **rules_python** (1.3.0) - Using upstream pip extensions (BEST PRACTICE)
- **rules_go** (0.57.0) - Clean BCR integration
- **rules_proto** (7.1.0) - Standard proto support
- **rules_rust** (0.63.0) - Would benefit from patch upstreaming

#### Utility Libraries:
- **fmt** (11.2.0) - Clean BCR migration success
- **spdlog** (1.15.3) - No custom modifications needed
- **xxhash** (0.8.3) - Simple BCR integration
- **nlohmann_json** (3.12.0) - Standard JSON library
- **yaml-cpp** (0.8.0) - Configuration parsing support

### Still in Extensions (Complex Dependencies)

These remain in module extensions due to patches or complex setup:

#### Patched Dependencies:
- **com_google_protobuf** - Extensive arena.h modifications
- **com_google_absl** - Custom compatibility patches  
- **com_github_grpc_grpc** - Envoy-specific modifications
- **rules_foreign_cc** - Platform-specific patches
- **emsdk** - WebAssembly toolchain patches

#### Envoy-Specific:
- **envoy_api** - Envoy's own API definitions
- **envoy_toolshed** - CI and build tooling
- **envoy_examples** - Example configurations
- **grpc_httpjson_transcoding** - Envoy-specific transcoding

#### Complex Toolchains:
- **Mobile dependencies** - Swift, Kotlin, Android SDK setup
- **FIPS modules** - Cryptographic compliance requirements  
- **Intel-specific libraries** - QAT, IPP, platform optimizations

### Recommended Next Steps

Following Bazel best practices, we should:

1. **Upstream patches to BCR**: Submit patches for widely-used libraries to reduce custom extensions
2. **Monitor BCR additions**: Migrate more dependencies as they become available
3. **Simplify extensions**: Break down large extensions into focused, single-purpose ones
4. **Eliminate WORKSPACE.bzlmod**: Move any remaining workspace logic to proper bzlmod patterns
5. **Documentation**: Create migration guides for downstream projects

## Troubleshooting and Common Issues

### Build Failures

**Issue**: `ERROR: no such package '@some_dep//...': Repository '@some_dep' is not defined`
```bash
# Solution: Check if dependency is properly declared
bazel mod explain @some_dep
bazel mod show_extension_repos | grep some_dep
```

**Issue**: Version conflicts between dependencies
```bash
# Debug version resolution
bazel mod graph | grep some_dep
# Bzlmod automatically resolves to highest compatible version
```

**Issue**: Extension not loading properly
```bash
# Check extension syntax
bazel build --nobuild //... 2>&1 | grep -i extension
# Verify extension file exists and is properly structured
```

### Migration Issues

**Issue**: `//external:dep` references not working
- **Solution**: Use `//third_party:dep` compatibility layer or migrate to `@repo//:target`

**Issue**: Native bindings not found in bzlmod mode
- **Solution**: These are intentionally disabled. Use direct `@repo//:target` references

**Issue**: Custom patches not applying
- **Solution**: Verify patches are in extensions, not trying to use BCR versions of patched dependencies

### Validation Commands

```bash
# Comprehensive dependency analysis
bazel mod graph > deps.txt
bazel mod show_extension_repos > extensions.txt

# Test core functionality
bazel build //source/common/common:version_lib
bazel test //test/common/common:version_test

# Check for WORKSPACE vs bzlmod differences
bazel build --enable_bzlmod //source/exe:envoy-static --nobuild
bazel build --noexperimental_enable_bzlmod //source/exe:envoy-static --nobuild
```

## Future Improvements

### Short Term (Next 6 months)
1. **Patch upstreaming**: Submit Envoy patches to BCR for widely-used dependencies
2. **Extension simplification**: Break down large extensions into focused units  
3. **WORKSPACE.bzlmod elimination**: Move remaining logic to proper bzlmod patterns
4. **Mobile module cleanup**: Streamline mobile-specific extensions

### Medium Term (6-12 months)  
1. **BCR contribution**: Work with Bazel team to add Envoy to BCR
2. **Toolchain migration**: Move complex toolchain setup to standard patterns
3. **Documentation consolidation**: Single source of truth for bzlmod usage
4. **Automated migration tools**: Scripts to help downstream projects migrate

### Long Term (1+ years)
1. **Full BCR ecosystem**: Reduce custom extensions to minimum necessary
2. **Bazel 8+ adoption**: Drop WORKSPACE support entirely
3. **Performance optimization**: Leverage bzlmod-specific performance features
4. **Community standards**: Establish patterns other C++ projects can follow

## Resources and References

### Official Bazel Documentation
- [Bzlmod Migration Guide](https://bazel.build/external/migration) - Official migration instructions
- [MODULE.bazel Reference](https://bazel.build/external/mod) - Complete syntax guide
- [Module Extensions Guide](https://bazel.build/external/extension) - Creating custom extensions
- [Bazel Central Registry](https://registry.bazel.build/) - Available modules

### Envoy-Specific Resources  
- [THIRD_PARTY_MIGRATION.md](THIRD_PARTY_MIGRATION.md) - Legacy reference migration
- [bazel/README.md](bazel/README.md) - Build system documentation
- [examples/bzlmod/](examples/bzlmod/) - Practical usage examples

### Community Resources
- [Bazel Slack #bzlmod channel](https://slack.bazel.build/) - Community support
- [BCR GitHub Repository](https://github.com/bazelbuild/bazel-central-registry) - Module registry
- [Bazel Blog: Bzlmod](https://blog.bazel.build/2022/05/10/bzlmod-preview.html) - Background and rationale

---

*Note: The remainder of this document contains detailed implementation notes and historical migration details. For most users, the sections above provide sufficient guidance for working with Envoy's bzlmod setup.*
# Bzlmod Examples and Quick Start

This directory provides practical examples for using Envoy with bzlmod (MODULE.bazel) instead of the traditional WORKSPACE system.

## Getting Started with Bzlmod

### Basic Project Setup

Create a new project using Envoy with bzlmod:

```starlark
# MODULE.bazel
module(name = "my_envoy_app", version = "1.0.0")

# Core dependencies from Bazel Central Registry
bazel_dep(name = "rules_cc", version = "0.2.8")
bazel_dep(name = "protobuf", version = "27.5")
bazel_dep(name = "abseil-cpp", version = "20241220.1")

# Envoy dependency (when available in BCR)
# bazel_dep(name = "envoy", version = "1.32.0")

# For development - using local path
bazel_dep(name = "envoy", version = "0.0.0-dev")
local_path_override(module_name = "envoy", path = "third_party/envoy")
```

### Exploring Dependencies

```bash
# View your project's dependency graph
bazel mod graph

# Show all available repositories
bazel mod show_extension_repos

# Check specific dependency resolution
bazel mod explain @envoy
```

### Building with Bzlmod

```bash
# Enable bzlmod for your builds
bazel build --enable_bzlmod //my_app:target

# Or set it permanently in .bazelrc
echo "build --enable_bzlmod" >> .bazelrc
bazel build //my_app:target
```

## Migration Examples

### Example 1: Simple Library Migration

**Before (WORKSPACE):**
```starlark
# WORKSPACE
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "com_google_absl",
    sha256 = "...",
    urls = ["https://github.com/abseil/abseil-cpp/archive/..."],
)
```

**After (MODULE.bazel):**
```starlark
# MODULE.bazel
bazel_dep(name = "abseil-cpp", version = "20241220.1")
```

### Example 2: Rules Migration

**Before (WORKSPACE):**
```starlark
# WORKSPACE
http_archive(
    name = "rules_python",
    sha256 = "...",
    urls = ["https://github.com/bazelbuild/rules_python/releases/..."],
)

load("@rules_python//python:repositories.bzl", "python_rules_dependencies")
python_rules_dependencies()
```

**After (MODULE.bazel):**
```starlark
# MODULE.bazel  
bazel_dep(name = "rules_python", version = "1.4.1")

# Python toolchain using upstream extension
python = use_extension("@rules_python//python/extensions:python.bzl", "python")
python.toolchain(python_version = "3.12")

# Python packages using upstream pip extension  
pip = use_extension("@rules_python//python/extensions:pip.bzl", "pip")
pip.parse(
    hub_name = "pip_deps",
    python_version = "3.12", 
    requirements_lock = "//requirements.txt"
)
use_repo(pip, "pip_deps")
```

### Example 3: Consolidated Extension Usage

Envoy uses **consolidated extensions** for complex dependencies that require patches:

```starlark
# MODULE.bazel
# Use Envoy's consolidated core extension for all complex dependencies
envoy_core = use_extension("@envoy//bazel/extensions:core.bzl", "core")

# The consolidated extension handles 100+ repositories with patches
use_repo(envoy_core, 
    "com_google_protobuf",  # With Envoy-specific patches
    "com_github_grpc_grpc", # With custom modifications
    "com_google_cel_cpp",   # With build configuration
    "proto_bazel_features", # Additional protobuf features
)

# Use consolidated toolchains extension for all imports and setups
envoy_toolchains = use_extension("@envoy//bazel/extensions:toolchains.bzl", "toolchains")
use_repo(envoy_toolchains,
    "envoy_repo",           # Repository metadata
    "grcov",               # Code coverage tooling
    "rules_fuzzing_oss_fuzz", # Fuzzing infrastructure
)
    "boringssl_fips"        # FIPS variant
)
```

## Best Practices

### 1. Prefer BCR Dependencies
Always check [Bazel Central Registry](https://registry.bazel.build/) first:
```starlark
# ✅ Good - using BCR
bazel_dep(name = "googletest", version = "1.17.0")

# ❌ Avoid if BCR version available  
my_deps = use_extension("//bazel:deps.bzl", "my_deps")
```

### 2. Use Upstream Extensions
Prefer official rule extensions over custom ones:
```starlark
# ✅ Good - upstream rules_python extension
python = use_extension("@rules_python//python/extensions:python.bzl", "python")

# ❌ Less preferred - custom extension
python_deps = use_extension("//tools:python_deps.bzl", "python_deps")
```

### 3. Use Consolidated Extensions
Envoy has streamlined its extension architecture for better maintainability:
```starlark
# ✅ Good - consolidated extensions
envoy_core = use_extension("@envoy//bazel/extensions:core.bzl", "core")
envoy_toolchains = use_extension("@envoy//bazel/extensions:toolchains.bzl", "toolchains")

# ❌ Deprecated - individual extensions (still functional but deprecated)
envoy_deps = use_extension("@envoy//bazel/extensions:dependencies.bzl", "dependencies")
envoy_deps_extra = use_extension("@envoy//bazel/extensions:dependencies_extra.bzl", "dependencies_extra")
```

### 4. Version Pinning
Pin to specific versions for reproducible builds:
```starlark
# ✅ Good - specific version
bazel_dep(name = "protobuf", version = "27.5")

# ❌ Avoid - floating versions not allowed in bzlmod anyway
```

### 5. Development Dependencies
Mark test/development-only dependencies:
```starlark
bazel_dep(name = "googletest", version = "1.17.0", dev_dependency = True)
bazel_dep(name = "rules_shellcheck", version = "0.3.3", dev_dependency = True)
```

## Troubleshooting

### Repository Not Found
```
ERROR: Repository '@missing_repo' not found
```
**Solution:** Check if the repository is provided by one of Envoy's consolidated extensions:
```starlark
# Add to use_repo for the appropriate extension
envoy_core = use_extension("@envoy//bazel/extensions:core.bzl", "core")
use_repo(envoy_core, "missing_repo")

# OR for toolchain repositories
envoy_toolchains = use_extension("@envoy//bazel/extensions:toolchains.bzl", "toolchains") 
use_repo(envoy_toolchains, "missing_repo")
```
```bash
bazel mod show_extension_repos | grep missing_repo
```

### Version Conflicts
```
ERROR: Version conflict for module 'some_module'
```
**Solution:** Bzlmod automatically resolves to the highest compatible version. Check the resolution:
```bash
bazel mod graph | grep some_module
```

### Extension Loading Errors
```
ERROR: Error in extension 'my_extension'
```
**Solution:** Verify extension syntax and that referenced files exist:
```bash
# Check extension definition
ls -la bazel/extensions/my_extension.bzl

# Test loading without building
bazel build --nobuild //... 2>&1 | grep -i extension
```

### Migration Testing
Test both WORKSPACE and bzlmod side by side:
```bash
# Test WORKSPACE build
bazel build --noexperimental_enable_bzlmod //...

# Test bzlmod build  
bazel build --enable_bzlmod //...

# Compare outputs
diff <(bazel build --noexperimental_enable_bzlmod //... 2>&1) \
     <(bazel build --enable_bzlmod //... 2>&1)
```

## Sample Projects

### Minimal C++ Application
```starlark
# MODULE.bazel
module(name = "envoy_hello_world", version = "1.0.0")

bazel_dep(name = "rules_cc", version = "0.2.8")
bazel_dep(name = "envoy", version = "0.0.0-dev")

local_path_override(module_name = "envoy", path = "../..")
```

```starlark
# BUILD.bazel
load("@rules_cc//cc:defs.bzl", "cc_binary")

cc_binary(
    name = "hello_envoy",
    srcs = ["main.cc"],
    deps = ["@envoy//source/common/common:version_lib"],
)
```

### Python Extension with Envoy
```starlark
# MODULE.bazel
module(name = "envoy_python_extension", version = "1.0.0")

bazel_dep(name = "rules_python", version = "1.4.1")
bazel_dep(name = "envoy", version = "0.0.0-dev")

python = use_extension("@rules_python//python/extensions:python.bzl", "python")
python.toolchain(python_version = "3.12")

pip = use_extension("@rules_python//python/extensions:pip.bzl", "pip")
pip.parse(
    hub_name = "pypi",
    python_version = "3.12",
    requirements_lock = "requirements.txt"
)
use_repo(pip, "pypi")
```

## Next Steps

1. **Start small**: Migrate one dependency at a time
2. **Test frequently**: Use `--enable_bzlmod` flag during transition  
3. **Check BCR regularly**: New modules are added frequently
4. **Share learnings**: Contribute back to the community

## Resources

- [Official Bazel Migration Guide](https://bazel.build/external/migration)
- [Bazel Central Registry](https://registry.bazel.build/)  
- [Envoy Migration Documentation](../docs/root/start/migrating/bzlmod.md)
- [Community Slack #bzlmod](https://slack.bazel.build/)
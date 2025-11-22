"""Module extensions for Envoy's non-module dependencies.

This file defines module extensions to support Envoy's bzlmod migration while
respecting existing WORKSPACE patches and custom BUILD files.

## Background

Bazel's new module system (bzlmod) allows for better dependency management, but
requires a migration from the traditional WORKSPACE-based approach. This extension
serves as a bridge during the migration.

## Implementation Pattern

Following the pattern established in api/bazel/extensions.bzl, this extension:
1. Calls envoy_dependencies(bzlmod=True) from bazel/repositories.bzl
2. The same function is used by WORKSPACE mode (with bzlmod=False)
3. When bzlmod=True, dependencies already in BCR are skipped via conditional checks

This approach:
- Avoids code duplication between WORKSPACE and bzlmod modes
- Maintains a single source of truth for dependency definitions
- Ensures both modes load dependencies identically
- Makes migration clear and reviewable

## Usage in MODULE.bazel

This extension is used in the root MODULE.bazel file as follows:

```python
# Regular dependencies
envoy_deps = use_extension("//bazel:extensions.bzl", "envoy_dependencies_extension")
use_repo(
    envoy_deps,
    "boringssl_fips",
    "com_github_grpc_grpc",
    # ... other non-module dependencies
)

# Development dependencies (testing, linting, etc.)
envoy_dev_deps = use_extension("//bazel:extensions.bzl", "envoy_dev_dependencies_extension", dev_dependency = True)
use_repo(
    envoy_dev_deps,
    "com_github_bazelbuild_buildtools",
    # ... other dev dependencies
)
```

## Maintenance

When adding new dependencies:
- Add them to envoy_dependencies() in bazel/repositories.bzl
- If the dependency is in BCR, wrap it with `if not bzlmod:`
- If patches are needed, add them to the function
- Update the use_repo() call in MODULE.bazel (regular or dev)

When removing dependencies:
- If moving to BCR, wrap existing calls with `if not bzlmod:`
- Update the use_repo() call in MODULE.bazel
- Document the change

## Bzlmod Migration Status

Dependencies already migrated to BCR (skipped when bzlmod=True):
- bazel_features, highway, fast_float
- zlib, zstd, org_brotli, re2
- protobuf, spdlog, fmt, yaml-cpp
- nlohmann_json, xxhash, gperftools, numactl
- flatbuffers, google_benchmark, googletest
- bazel_gazelle, io_bazel_rules_go
- platforms, rules_shell, rules_cc, rules_foreign_cc
- boringssl (non-FIPS), emsdk
- rules_fuzzing, rules_license, rules_pkg, rules_shellcheck
- aspect_bazel_lib, abseil-cpp, rules_ruby
- toolchains_llvm (using git_override for specific commit)

See MODULE.bazel for the complete list of bazel_dep() entries.
"""

load("@envoy_api//bazel:envoy_http_archive.bzl", "envoy_http_archive")
load("@envoy_api//bazel:external_deps.bzl", "load_repository_locations")
load("@envoy_toolshed//repository:utils.bzl", "arch_alias")
load(":repo.bzl", "envoy_repo")
load(":repositories.bzl", "envoy_dependencies", "external_http_archive")
load(":repository_locations.bzl", "REPOSITORY_LOCATIONS_SPEC")

def _envoy_dependencies_impl(module_ctx):
    """Implementation of the envoy_dependencies module extension.

    This extension calls envoy_dependencies(bzlmod=True) which loads all Envoy
    dependencies. Dependencies already available in BCR are skipped via conditional
    checks within the function.

    Args:
        module_ctx: The module extension context
    """
    envoy_dependencies(bzlmod = True)

def _envoy_dev_dependencies_impl(module_ctx):
    """Implementation of the envoy_dev_dependencies module extension.

    This extension loads development-only dependencies (testing, linting, formatting).
    These are separated to avoid loading dev tools in production builds.

    Args:
        module_ctx: The module extension context
    """

    # Bazel buildtools for BUILD file formatting and linting
    external_http_archive("com_github_bazelbuild_buildtools")

def _envoy_repo_impl(module_ctx):
    """Implementation of the envoy_repo module extension.

    This extension creates the envoy_repo repository which provides version
    information and container metadata for RBE builds.

    Args:
        module_ctx: The module extension context
    """
    envoy_repo()

def _envoy_toolchains_impl(module_ctx):
    """Implementation of the envoy_toolchains module extension.

    This extension registers toolchains needed for Envoy builds in bzlmod mode,
    including the clang_platform alias used in various BUILD files.

    In WORKSPACE mode, this is handled by calling envoy_toolchains() from WORKSPACE.
    In bzlmod mode, we need to use this extension to make the same repositories available.

    Args:
        module_ctx: The module extension context
    """
    # Create the clang_platform repository using arch_alias
    # Note: We can't call envoy_toolchains() directly here because it uses native.register_toolchains
    # which is not allowed in module extensions. Instead, we only create the arch_alias repository.
    arch_alias(
        name = "clang_platform",
        aliases = {
            "amd64": "@envoy//bazel/platforms/rbe:linux_x64",
            "aarch64": "@envoy//bazel/platforms/rbe:linux_arm64",
        },
    )

# Define the module extensions
envoy_dependencies_extension = module_extension(
    implementation = _envoy_dependencies_impl,
    doc = """
    Extension for Envoy runtime dependencies not available in BCR or requiring patches.

    This extension calls the same envoy_dependencies() function used by WORKSPACE mode,
    but with bzlmod=True. This ensures both build systems load dependencies identically,
    making the migration clear and reviewable.

    Dependencies already in BCR are skipped automatically via conditional checks.
    For WORKSPACE mode, call envoy_dependencies() directly from WORKSPACE.

    See the module documentation above for maintenance guidelines.
    """,
)

envoy_dev_dependencies_extension = module_extension(
    implementation = _envoy_dev_dependencies_impl,
    doc = """
    Extension for Envoy development dependencies not available in BCR.

    This extension is for dev-only tools like testing frameworks, linters, and
    formatters. These are separated from runtime dependencies to avoid loading
    dev tools in production builds.

    Currently loads:
    - com_github_bazelbuild_buildtools: BUILD file formatting and linting
    """,
)

envoy_repo_extension = module_extension(
    implementation = _envoy_repo_impl,
    doc = """
    Extension for the envoy_repo repository.

    This extension creates the @envoy_repo repository which provides:
    - Version information (VERSION, API_VERSION)
    - Container metadata for RBE builds (containers.bzl)
    - LLVM compiler configuration
    - Repository path information

    This is required for RBE toolchain configuration and various build utilities.
    """,
)

envoy_toolchains_extension = module_extension(
    implementation = _envoy_toolchains_impl,
    doc = """
    Extension for Envoy toolchain setup in bzlmod mode.

    This extension creates toolchain-related repositories needed for Envoy builds:
    - clang_platform: Architecture-specific platform aliases for RBE builds

    In WORKSPACE mode, these are created by calling envoy_toolchains() from WORKSPACE.
    In bzlmod mode, this extension provides the same functionality.

    Note: Toolchain registration itself is handled by MODULE.bazel using the LLVM
    toolchain extension. This extension only creates auxiliary repositories.
    """,
)

"""Module extensions for Envoy Mobile's non-module dependencies.

This file defines module extensions to support Envoy Mobile's bzlmod migration
while respecting existing WORKSPACE configuration.

## Implementation Pattern

Following the pattern from the main Envoy module and api module, this extension:
1. Calls envoy_mobile_repositories(bzlmod=True) from bazel/envoy_mobile_repositories.bzl
2. The same function is used by WORKSPACE mode (with bzlmod=False)
3. When bzlmod=True, dependencies already in BCR are skipped via conditional checks

This approach:
- Avoids code duplication between WORKSPACE and bzlmod modes
- Maintains a single source of truth for dependency definitions
- Makes migration clear and reviewable

## Usage in MODULE.bazel

This extension is used in mobile/MODULE.bazel as follows:

```python
mobile_deps = use_extension("//bazel:extensions.bzl", "envoy_mobile_dependencies")
use_repo(
    mobile_deps,
    "com_github_libevent_libevent",
    "DrString",
    "SwiftLint",
    # ... other non-module dependencies
)
```

## Bzlmod Migration Status

Dependencies already migrated to BCR (skipped when bzlmod=True):
- rules_android, rules_android_ndk
- rules_apple, rules_swift
- rules_java, rules_jvm_external
- rules_kotlin, rules_proto_grpc, rules_detekt
- google_bazel_common (using git_override for specific commit)

See mobile/MODULE.bazel for the complete list of bazel_dep() entries.
"""

load(":envoy_mobile_repositories.bzl", "envoy_mobile_repositories")

def _envoy_mobile_dependencies_impl(module_ctx):
    """Implementation of the envoy_mobile_dependencies module extension.

    This extension calls envoy_mobile_repositories(bzlmod=True) which loads all
    Envoy Mobile dependencies. Dependencies already in BCR are skipped via
    conditional checks within the function.

    Args:
        module_ctx: The module extension context
    """
    envoy_mobile_repositories(bzlmod = True)

# Define the module extension
envoy_mobile_dependencies = module_extension(
    implementation = _envoy_mobile_dependencies_impl,
    doc = """
    Extension for Envoy Mobile dependencies not available in BCR.

    This extension calls the same envoy_mobile_repositories() function used by
    WORKSPACE mode, but with bzlmod=True. This ensures both build systems load
    dependencies identically.

    Dependencies already in BCR are skipped automatically via conditional checks.
    For WORKSPACE mode, call envoy_mobile_repositories() directly from WORKSPACE.
    """,
)

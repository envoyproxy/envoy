"""Core extension for Envoy Mobile dependencies and repositories (bzlmod-only).

This extension is for BZLMOD mode only and should never be called from WORKSPACE.
It provides mobile-specific repositories not available in Bazel Central Registry.

For WORKSPACE mode, use the functions in //bazel:repositories.bzl instead.
"""

load("//bazel:envoy_mobile_repositories.bzl", "envoy_mobile_repositories")

def _core_impl(module_ctx):
    """Implementation for core extension (bzlmod-only).

    This extension provides mobile dependencies and repository setup
    for Envoy Mobile applications. In bzlmod mode, we only call
    repository setup functions, not dependency initialization functions
    since those are handled by MODULE.bazel declarations.
    
    This is bzlmod-only - do not call from WORKSPACE files.
    """

    # Call the mobile repositories function to create repositories
    # that are not available in BCR
    envoy_mobile_repositories()

# Module extension for mobile core functionality (bzlmod-only)
core = module_extension(
    implementation = _core_impl,
    doc = """
    Core extension for Envoy Mobile dependencies and repositories (bzlmod-only).
    
    This extension provides mobile-specific repositories not in BCR.
    For WORKSPACE mode, use //bazel:repositories.bzl functions instead.
    This extension should never be called from WORKSPACE files.
    """,
)

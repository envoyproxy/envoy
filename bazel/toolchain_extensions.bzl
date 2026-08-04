"""Module extension for Envoy toolchain setup.

This extension is in a separate file from extensions.bzl because toolchains.bzl
has a top-level load from @envoy_repo//:compiler.bzl. The @envoy_repo repository
is created by the envoy_repo_ext extension in extensions.bzl, so it must exist
before this file can be loaded.

In MODULE.bazel, use_extension for this file must appear after use_repo for
envoy_repo from envoy_repo_ext.
"""

load("//bazel:toolchains.bzl", "envoy_toolchains")

def _envoy_toolchains_impl(module_ctx):
    """Implementation for envoy_toolchains_ext extension.

    Registers the LLVM/Clang toolchain, GCC RBE toolchain, and sets up
    platform-specific build configurations.

    Args:
        module_ctx: Module extension context
    """
    envoy_toolchains()

envoy_toolchains_ext = module_extension(
    implementation = _envoy_toolchains_impl,
    doc = """
    Extension for Envoy toolchain registration.

    This extension sets up:
    - LLVM/Clang toolchain via toolchains_llvm (hermetic or local)
    - GCC RBE toolchain configuration
    - Platform aliases for cross-compilation

    For WORKSPACE mode, call envoy_toolchains() directly from WORKSPACE.
    This extension should only be used in MODULE.bazel files.
    """,
)

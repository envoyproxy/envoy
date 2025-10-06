"""Toolchains extension for Envoy Mobile platform toolchains (bzlmod-only).

This extension is for BZLMOD mode only and should never be called from WORKSPACE.
It provides minimal custom mobile toolchain setup.

For WORKSPACE mode, use the functions in //bazel:repositories.bzl instead.

This extension only handles:
- Mobile toolchains registration
- Android configuration for bzlmod mode
"""

load("//bazel:android_configure.bzl", "android_configure")
load("//bazel:envoy_mobile_toolchains.bzl", "envoy_mobile_toolchains")

def _toolchains_impl(module_ctx):
    """Implementation for toolchains extension (bzlmod-only).

    This extension provides minimal mobile toolchain setup for cases not covered
    by native Android extensions. In bzlmod mode, Android SDK/NDK configuration
    is handled by native extensions from rules_android and rules_android_ndk.

    This is bzlmod-only - do not call from WORKSPACE files.
    """

    # Call the mobile toolchains function for platform registration
    envoy_mobile_toolchains()

    # Android configuration for bzlmod mode
    # In bzlmod mode, this can be overridden by native android_sdk_repository_extension
    # and android_ndk_repository_extension if configured
    android_configure(
        name = "local_config_android",
        build_tools_version = "30.0.2",
        ndk_api_level = 23,
        sdk_api_level = 30,
    )

# Module extension for mobile toolchains functionality (bzlmod-only)
toolchains = module_extension(
    implementation = _toolchains_impl,
    doc = """
    Minimal toolchains extension for Envoy Mobile platform setup (bzlmod-only).
    
    This extension provides mobile toolchain registration.
    For WORKSPACE mode, use //bazel:repositories.bzl functions instead.
    This extension should never be called from WORKSPACE files.
    
    Android SDK/NDK configuration is handled by native extensions from
    rules_android and rules_android_ndk in bzlmod mode.
    """,
)

"""Toolchains extension for Envoy Mobile platform toolchains and workspace setup.

This extension provides minimal custom mobile toolchain setup, as Android SDK/NDK
configuration now uses native Bazel Central Registry extensions from rules_android
and rules_android_ndk.

This extension only handles:
- Mobile toolchains registration
- WORKSPACE mode compatibility for Android configuration
"""

load("//bazel:android_configure.bzl", "android_configure")
load("//bazel:envoy_mobile_toolchains.bzl", "envoy_mobile_toolchains")

def _toolchains_impl(module_ctx):
    """Implementation for toolchains extension.

    This extension provides minimal mobile toolchain setup for cases not covered
    by native Android extensions. In bzlmod mode, Android SDK/NDK configuration
    is handled by native extensions from rules_android and rules_android_ndk.

    For WORKSPACE compatibility, this still provides the android_configure fallback.
    """

    # Call the mobile toolchains function for platform registration
    envoy_mobile_toolchains()

    # Provide Android configuration fallback for WORKSPACE mode
    # In bzlmod mode, this is overridden by native android_sdk_repository_extension
    # and android_ndk_repository_extension
    android_configure(
        name = "local_config_android",
        build_tools_version = "30.0.2",
        ndk_api_level = 23,
        sdk_api_level = 30,
    )

# Module extension for mobile toolchains functionality
# Note: Android SDK/NDK configuration now handled by native extensions
toolchains = module_extension(
    implementation = _toolchains_impl,
    doc = """
    Minimal toolchains extension for Envoy Mobile platform setup.
    
    This extension provides mobile toolchain registration and WORKSPACE
    compatibility. Android SDK/NDK configuration is now handled by native
    extensions from rules_android and rules_android_ndk in bzlmod mode.
    """,
)

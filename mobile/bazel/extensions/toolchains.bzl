"""Toolchains extension for Envoy Mobile platform toolchains and workspace setup.

This extension provides:
- Mobile toolchains configuration
- Android SDK/NDK configuration
- Mobile workspace setup
- Platform-specific setup
"""

load("//bazel:envoy_mobile_toolchains.bzl", "envoy_mobile_toolchains")
load("//bazel:android_configure.bzl", "android_configure")
load("//bazel:envoy_mobile_workspace.bzl", "envoy_mobile_workspace")

def _post_android_setup_impl(repository_ctx):
    """Repository rule to handle post-android configuration setup."""

    # This will be called after @local_config_android is available
    repository_ctx.file("BUILD.bazel", "")
    repository_ctx.file("WORKSPACE", "")

    # Create a dummy file to indicate setup is complete
    repository_ctx.file("setup_complete.txt", "Android workspace setup complete")

_post_android_setup = repository_rule(
    implementation = _post_android_setup_impl,
)

def _toolchains_impl(module_ctx):
    """Implementation for toolchains extension.

    This extension provides mobile toolchain registration, Android
    configuration, and workspace setup for Envoy Mobile applications.
    """

    # Call the mobile toolchains function
    envoy_mobile_toolchains()
    
    # Configure Android SDK/NDK with the same parameters as used in WORKSPACE.bzlmod
    android_configure(
        name = "local_config_android",
        build_tools_version = "30.0.2",
        ndk_api_level = 23,
        sdk_api_level = 30,
    )
    
    # Create a marker repository to ensure android workspace setup completes
    _post_android_setup(name = "envoy_android_workspace_setup")
    
    # Call the mobile workspace function
    envoy_mobile_workspace()

# Module extension for mobile toolchains functionality
toolchains = module_extension(
    implementation = _toolchains_impl,
    doc = """
    Toolchains extension for Envoy Mobile platform setup.
    
    This extension provides mobile toolchain registration, Android
    configuration, and workspace setup for Envoy Mobile applications,
    following bzlmod best practices.
    """,
)
"""Core extension for Envoy Mobile dependencies and repositories.

This extension provides:
- Mobile dependencies and repository setup
- API dependencies from envoy_api module
- Core mobile functionality
"""

load("//bazel:envoy_mobile_dependencies.bzl", "envoy_mobile_dependencies")
load("//bazel:envoy_mobile_repositories.bzl", "envoy_mobile_repositories")

def _core_impl(module_ctx):
    """Implementation for core extension.

    This extension provides mobile dependencies and repository setup
    for Envoy Mobile applications.
    """

    # Call the mobile dependencies function
    envoy_mobile_dependencies()

    # Call the mobile repositories function
    envoy_mobile_repositories()

# Module extension for mobile core functionality
core = module_extension(
    implementation = _core_impl,
    doc = """
    Core extension for Envoy Mobile dependencies and repositories.
    
    This extension provides mobile dependencies and repository setup
    for Envoy Mobile applications, following bzlmod best practices.
    """,
)

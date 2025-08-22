"""Extension for Envoy API dependencies."""

load("//bazel:repositories.bzl", "api_dependencies")

def _api_dependencies_impl(module_ctx):
    """Implementation for api_dependencies extension.

    This extension wraps the api_dependencies() function to make it
    available as a bzlmod module extension.
    """

    # Call the API dependencies function
    api_dependencies()

# Module extension for api_dependencies
envoy_api_deps = module_extension(
    implementation = _api_dependencies_impl,
    doc = """
    Extension for Envoy API dependencies.
    
    This extension wraps the api_dependencies() function to make it
    available as a bzlmod module extension, handling API-specific
    repository definitions.
    """,
)

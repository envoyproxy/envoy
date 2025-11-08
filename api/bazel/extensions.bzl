"""Non-BCR dependencies for Envoy Data Plane API.

This extension provides repositories that are not available in Bazel Central Registry (BCR).
"""

load(":repositories.bzl", "api_dependencies")

def _non_module_deps_impl(module_ctx):
    """Implementation for non_module_deps extension.

    This extension calls api_dependencies(bzlmod=True) which creates repositories
    not in BCR. It safely coexists with BCR deps because envoy_http_archive
    checks native.existing_rules() before creating repositories.

    Args:
        module_ctx: Module extension context
    """
    api_dependencies(bzlmod = True)

non_module_deps = module_extension(
    implementation = _non_module_deps_impl,
    doc = """
    Extension for Envoy API dependencies not available in BCR.

    This extension creates the following repositories:
    - prometheus_metrics_model: Prometheus client model
    - com_github_chrusty_protoc_gen_jsonschema: Proto to JSON schema compiler
    - envoy_toolshed: Tooling and libraries for Envoy development

    For WORKSPACE mode, call api_dependencies() directly from WORKSPACE.
    This extension should only be used in MODULE.bazel files.
    """,
)

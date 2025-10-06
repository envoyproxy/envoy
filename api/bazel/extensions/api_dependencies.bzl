"""Extension for Envoy API dependencies (bzlmod-only).

This extension is for BZLMOD mode only and should never be called from WORKSPACE.
It creates API-specific repositories that are not available in Bazel Central Registry.

For WORKSPACE mode, use the functions in //bazel:repositories.bzl instead.
"""

load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _api_dependencies_impl(module_ctx):
    """Implementation for api_dependencies extension (bzlmod-only).

    This extension provides API-specific dependencies that are not available
    in BCR or need custom BUILD files. This is bzlmod-only - do not call
    from WORKSPACE files.
    """

    # CNCF XDS repository with proper template substitution
    if not native.existing_rule("com_github_cncf_xds"):
        http_archive(
            name = "com_github_cncf_xds",
            sha256 = "790c4c83b6950bb602fec221f6a529d9f368cdc8852aae7d2592d0d04b015f37",
            strip_prefix = "xds-2ac532fd44436293585084f8d94c6bdb17835af0",
            urls = ["https://github.com/cncf/xds/archive/2ac532fd44436293585084f8d94c6bdb17835af0.tar.gz"],
        )
    
    # Prometheus metrics model 
    if not native.existing_rule("prometheus_metrics_model"):
        http_archive(
            name = "prometheus_metrics_model",
            sha256 = "fbe882578c95e3f2c9e7b23bfac104b8d82a824b7b7b59b95b8a4b87acf688b2",
            strip_prefix = "client_model-0.6.1",
            urls = ["https://github.com/prometheus/client_model/archive/v0.6.1.tar.gz"],
            build_file_content = PROMETHEUSMETRICS_BUILD_CONTENT,
        )

PROMETHEUSMETRICS_BUILD_CONTENT = """
load("@envoy_api//bazel:api_build_system.bzl", "api_cc_py_proto_library")
load("@io_bazel_rules_go//proto:def.bzl", "go_proto_library")

api_cc_py_proto_library(
    name = "client_model",
    srcs = [
        "io/prometheus/client/metrics.proto",
    ],
    visibility = ["//visibility:public"],
)

go_proto_library(
    name = "client_model_go_proto",
    importpath = "github.com/prometheus/client_model/go",
    proto = ":client_model",
    visibility = ["//visibility:public"],
)
"""

# Module extension for api_dependencies (bzlmod-only)
envoy_api_deps = module_extension(
    implementation = _api_dependencies_impl,
    doc = """
    Extension for Envoy API dependencies (bzlmod-only).
    
    This extension creates API-specific repositories not in BCR.
    For WORKSPACE mode, use //bazel:repositories.bzl functions instead.
    This extension should never be called from WORKSPACE files.
    """,
)

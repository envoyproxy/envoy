load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements the grpc-web protocol (https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md)
# Public docs: docs/root/configuration/http_filters/grpc_web_filter.rst

envoy_package()

envoy_cc_library(
    name = "grpc_web_filter_lib",
    srcs = ["grpc_web_filter.cc"],
    hdrs = ["grpc_web_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:base64_lib",
        "//source/common/common:utility_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/grpc_web:grpc_web_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_web/v3:pkg_cc_proto",
    ],
)

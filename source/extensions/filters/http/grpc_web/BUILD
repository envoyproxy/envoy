licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements the grpc-web protocol (https://github.com/grpc/grpc/blob/master/doc/PROTOCOL-WEB.md)
# Public docs: docs/root/configuration/http_filters/grpc_web_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_google_grpc",
)

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
        "//source/common/grpc:common_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
        "//source/extensions/filters/http/grpc_web:grpc_web_filter_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that bridges HTTP/1.1 unary "gRPC" to compliant HTTP/2 gRPC.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/grpc_http1_bridge_filter

envoy_extension_package()

envoy_cc_library(
    name = "http1_bridge_filter_lib",
    srcs = ["http1_bridge_filter.cc"],
    hdrs = ["http1_bridge_filter.h"],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:utility_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:context_lib",
        "//source/common/http:headers_lib",
        "//source/common/http/http1:codec_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_http1_bridge/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/grpc_http1_bridge:http1_bridge_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_http1_bridge/v3:pkg_cc_proto",
    ],
)

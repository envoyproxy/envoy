licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements gRPC telemetry

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_google_grpc",
)

envoy_package()

envoy_cc_library(
    name = "config",
    srcs = ["grpc_stats_filter.cc"],
    hdrs = ["grpc_stats_filter.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:context_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/config/filter/http/grpc_stats/v2alpha:pkg_cc_proto",
    ],
)

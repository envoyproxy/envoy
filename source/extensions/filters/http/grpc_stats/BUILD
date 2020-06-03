load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements gRPC telemetry

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["grpc_stats_filter.cc"],
    hdrs = ["grpc_stats_filter.h"],
    security_posture = "unknown",
    status = "alpha",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:context_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_stats/v3:pkg_cc_proto",
    ],
)

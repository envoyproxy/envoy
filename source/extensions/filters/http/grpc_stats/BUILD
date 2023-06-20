load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter that implements gRPC telemetry

envoy_extension_package()

envoy_cc_library(
    name = "response_frame_counter_lib",
    srcs = ["response_frame_counter.cc"],
    hdrs = ["response_frame_counter.h"],
    deps = [
        "//source/common/grpc:codec_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/runtime:runtime_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["grpc_stats_filter.cc"],
    hdrs = ["grpc_stats_filter.h"],
    deps = [
        ":response_frame_counter_lib",
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//envoy/stream_info:filter_state_interface",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:context_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/stream_info:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/grpc_stats/v3:pkg_cc_proto",
    ],
)

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
    name = "message_counter_lib",
    srcs = ["message_counter.cc"],
    hdrs = ["message_counter.h"],
    deps = ["//include/envoy/buffer:buffer_interface"],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":message_counter_lib",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/grpc:common_lib",
        "//source/common/grpc:context_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:empty_http_filter_config_lib",
    ],
)

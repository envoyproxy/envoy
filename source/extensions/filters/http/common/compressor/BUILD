licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "compressor_lib",
    srcs = ["compressor.cc"],
    hdrs = ["compressor.h"],
    deps = [
        "//include/envoy/compressor:compressor_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/stream_info:filter_state_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/common/protobuf",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

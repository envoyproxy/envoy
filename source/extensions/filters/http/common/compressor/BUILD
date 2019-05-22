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
        "//include/envoy/http:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/common/protobuf",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "passthrough_lib",
    srcs = ["passthrough.cc"],
    hdrs = ["passthrough.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
    ],
)

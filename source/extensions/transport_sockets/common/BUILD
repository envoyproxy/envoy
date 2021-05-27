load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "passthrough_lib",
    srcs = ["passthrough.cc"],
    hdrs = ["passthrough.h"],
    deps = [
        "//include/envoy/network:connection_interface_no_include_prefix",
        "//include/envoy/network:transport_socket_interface_no_include_prefix",
        "//source/common/buffer:buffer_lib",
    ],
)

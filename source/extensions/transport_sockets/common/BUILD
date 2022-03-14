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
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:transport_socket_options_lib",
    ],
)

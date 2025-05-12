load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "io_uring_interface",
    hdrs = [
        "io_uring.h",
    ],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:address_interface",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "reverse_connection_utility_lib",
    srcs = ["reverse_connection_utility.cc"],
    hdrs = ["reverse_connection_utility.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "@com_google_absl//absl/strings",
    ],
)

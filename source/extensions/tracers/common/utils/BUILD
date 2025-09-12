load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "trace_lib",
    srcs = ["trace.cc"],
    hdrs = ["trace.h"],
    deps = [
        "//envoy/common:time_interface",
        "@com_google_absl//absl/strings",
    ],
)

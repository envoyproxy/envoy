licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "local_info_lib",
    hdrs = ["local_info_impl.h"],
    deps = ["//include/envoy/local_info:local_info_interface"],
)

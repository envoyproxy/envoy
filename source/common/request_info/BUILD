licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "request_info_lib",
    hdrs = ["request_info_impl.h"],
    deps = ["//include/envoy/request_info:request_info_interface"],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    deps = ["//include/envoy/request_info:request_info_interface"],
)

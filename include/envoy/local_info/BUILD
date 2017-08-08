licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "local_info_interface",
    hdrs = ["local_info.h"],
    external_deps = ["envoy_base"],
    deps = ["//include/envoy/network:address_interface"],
)

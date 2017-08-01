licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "hot_restart_nop_lib",
    hdrs = ["hot_restart_nop.h"],
    deps = [
        "//include/envoy/server:hot_restart_interface",
    ],
)

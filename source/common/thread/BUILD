licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "thread_singleton_lib",
    srcs = ["thread_singleton.cc"],
    deps = ["//include/envoy/thread:thread_interface"],
)

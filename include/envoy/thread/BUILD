load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "thread_interface",
    hdrs = ["thread.h"],
    deps = ["//source/common/common:thread_annotations"],
)

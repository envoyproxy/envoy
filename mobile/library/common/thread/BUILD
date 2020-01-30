licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

envoy_package()

envoy_cc_library(
    name = "lock_guard_lib",
    hdrs = ["lock_guard.h"],
    repository = "@envoy",
    deps = [
        "@envoy//include/envoy/thread:thread_interface",
        "@envoy//source/common/common:thread_annotations",
    ],
)

package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "api_interface",
    hdrs = ["api.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/thread:thread_interface",
    ],
)

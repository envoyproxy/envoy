load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "terminate_thread_lib",
    srcs = ["terminate_thread.cc"],
    hdrs = ["terminate_thread.h"],
    deps = [
        "//envoy/thread:thread_interface",
        "//source/common/common:minimal_logger_lib",
    ],
)

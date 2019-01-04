licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "thread_factory_singleton_lib",
    srcs = ["thread_factory_singleton.cc"],
    deps = [
        "//include/envoy/thread:thread_interface",
        "//source/common/common:assert_lib",
    ],
)

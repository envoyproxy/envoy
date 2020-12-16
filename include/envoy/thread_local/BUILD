load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "thread_local_interface",
    hdrs = ["thread_local.h"],
    deps = [
        ":thread_local_object",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/common:assert_lib",
        "//source/common/singleton:threadsafe_singleton",
    ],
)

envoy_cc_library(
    name = "thread_local_object",
    hdrs = ["thread_local_object.h"],
    deps = ["//source/common/common:assert_lib"],
)

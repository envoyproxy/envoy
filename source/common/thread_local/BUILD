load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "thread_local_lib",
    srcs = ["thread_local_impl.cc"],
    hdrs = ["thread_local_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:stl_helpers",
    ],
)

package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "thread_local_lib",
    srcs = ["thread_local_impl.cc"],
    hdrs = ["thread_local_impl.h"],
    deps = [
        "//include/envoy/event:dispatcher_includes",
        "//include/envoy/thread_local:thread_local_includes",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:stl_helpers",
    ],
)

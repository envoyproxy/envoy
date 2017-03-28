package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "stats_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/stats:stats_includes",
        "//include/envoy/thread_local:thread_local_includes",
        "//source/common/stats:stats_lib",
        "//test/mocks:common_lib",
    ],
)

package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "init_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/init:init_includes",
        "//test/mocks:common_lib",
    ],
)

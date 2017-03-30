package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_mock")

envoy_cc_mock(
    name = "buffer_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//test/test_common:utility_lib",
    ],
)

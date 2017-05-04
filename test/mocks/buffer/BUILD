licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "buffer_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//test/test_common:utility_lib",
    ],
)

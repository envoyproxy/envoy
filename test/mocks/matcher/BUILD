load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "matcher_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/common:matchers_lib",
        "//source/common/matcher:matcher_lib",
    ],
)

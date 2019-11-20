licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "ratelimit_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/ratelimit:ratelimit_interface",
    ],
)

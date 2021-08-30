load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "ratelimit_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/ratelimit:ratelimit_interface",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "config_mocks",
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/config:subscription_interface",
    ],
)

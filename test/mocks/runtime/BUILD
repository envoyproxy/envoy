licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "runtime_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/common:optional",
        "//include/envoy/runtime:runtime_interface",
        "//test/mocks:common_lib",
    ],
)

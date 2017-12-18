licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "request_info_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//include/envoy/request_info:request_info_interface",
        "//include/envoy/upstream:upstream_interface",
        "//test/mocks/upstream:host_mocks",
    ],
)

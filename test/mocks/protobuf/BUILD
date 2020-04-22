licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)

envoy_package()

envoy_cc_mock(
    name = "protobuf_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = ["//include/envoy/protobuf:message_validator_interface"],
)

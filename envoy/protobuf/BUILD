load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "message_validator_interface",
    hdrs = ["message_validator.h"],
    deps = [
        "//envoy/common:exception_lib",
        "//envoy/runtime:runtime_interface",
        "//source/common/protobuf",
    ],
)

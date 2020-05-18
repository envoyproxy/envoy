licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "message_validator_interface",
    hdrs = ["message_validator.h"],
    deps = [
        "//source/common/common:documentation_url_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:macros",
        "//source/common/protobuf",
    ],
)

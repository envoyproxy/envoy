load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "system_helper_interface_lib",
    srcs = ["system_helper.cc"],
    hdrs = ["system_helper.h"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/cert_validator/platform_bridge:platform_bridge_cert_validator_lib",
    ],
)

envoy_cc_library(
    name = "lambda_logger_delegate_lib",
    srcs = ["lambda_logger_delegate.cc"],
    hdrs = ["lambda_logger_delegate.h"],
    repository = "@envoy",
    deps = [
        "//library/common/api:external_api_lib",
        "//library/common/bridge:utility_lib",
        "//library/common/data:utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:macros",
        "@envoy//source/common/common:minimal_logger_lib",
    ],
)

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "system_helper_lib",
    srcs = [
        "default_system_helper.cc",
        "system_helper.cc",
    ],
    hdrs = [
        "default_system_helper.h",
        "system_helper.h",
    ],
    repository = "@envoy",
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

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_library", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_library(
    name = "logger_delegate_lib",
    srcs = ["logger_delegate.cc"],
    hdrs = ["logger_delegate.h"],
    repository = "@envoy",
    deps = [
        "//library/common:engine_types_lib",
        "//library/common/api:external_api_lib",
        "//library/common/bridge:utility_lib",
        "//library/common/types:c_types_lib",
        "@envoy//source/common/common:macros",
        "@envoy//source/common/common:minimal_logger_lib",
    ],
)

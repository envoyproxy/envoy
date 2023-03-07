load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "lambda_logger_delegate_test",
    srcs = ["lambda_logger_delegate_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/api:external_api_lib",
        "//library/common/common:lambda_logger_delegate_lib",
        "//library/common/data:utility_lib",
        "//library/common/types:c_types_lib",
    ],
)

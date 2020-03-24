licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "aws_lambda_filter_test",
    srcs = ["aws_lambda_filter_test.cc"],
    extension_name = "envoy.filters.http.aws_lambda",
    deps = [
        "//source/extensions/filters/http/aws_lambda:aws_lambda_filter_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/http:http_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "aws_lambda_filter_integration_test",
    srcs = ["aws_lambda_filter_integration_test.cc"],
    # TODO(marcomagdy): Excluding this test from coverage until we figure out why it fails *only* during
    # the CI coverage phase (and only there). It passes in other CI test jobs (asan/tsan) and locally every time.
    coverage = False,
    extension_name = "envoy.filters.http.aws_lambda",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/aws_lambda:aws_lambda_filter_lib",
        "//source/extensions/filters/http/aws_lambda:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "arn_test",
    srcs = ["arn_test.cc"],
    extension_name = "envoy.filters.http.aws_lambda",
    deps = [
        "//source/extensions/filters/http/aws_lambda:aws_lambda_filter_lib",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.aws_lambda",
    deps = [
        "//source/extensions/filters/http/aws_lambda:config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/aws_lambda/v3:pkg_cc_proto",
    ],
)

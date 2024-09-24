load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "aws_lambda_filter_test",
    srcs = ["aws_lambda_filter_test.cc"],
    extension_names = ["envoy.filters.http.aws_lambda"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/aws_lambda:aws_lambda_filter_lib",
        "//test/extensions/common/aws:aws_mocks",
        "//test/mocks/http:http_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "aws_lambda_filter_integration_test",
    size = "large",
    srcs = ["aws_lambda_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.aws_lambda"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/aws_lambda:aws_lambda_filter_lib",
        "//source/extensions/filters/http/aws_lambda:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "arn_test",
    srcs = ["arn_test.cc"],
    extension_names = ["envoy.filters.http.aws_lambda"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/aws_lambda:aws_lambda_filter_lib",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.aws_lambda"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/aws_lambda:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/extensions/filters/http/aws_lambda/v3:pkg_cc_proto",
    ],
)

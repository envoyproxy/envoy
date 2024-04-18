load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

# L7 HTTP AWS Lambda filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/aws_lambda_filter

envoy_extension_package()

envoy_proto_library(
    name = "request_response",
    srcs = ["request_response.proto"],
)

envoy_cc_library(
    name = "aws_lambda_filter_lib",
    srcs = ["aws_lambda_filter.cc"],
    hdrs = ["aws_lambda_filter.h"],
    deps = [
        ":request_response_cc_proto",
        "//envoy/http:filter_interface",
        "//source/common/common:base64_lib",
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:sigv4_signer_impl_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":aws_lambda_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/aws_lambda/v3:pkg_cc_proto",
    ],
)

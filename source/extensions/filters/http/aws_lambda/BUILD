licenses(["notice"])  # Apache 2

# L7 HTTP AWS Lambda filter
# Public docs: docs/root/configuration/http_filters/aws_lambda_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "aws_lambda_filter_lib",
    srcs = ["aws_lambda_filter.cc"],
    hdrs = ["aws_lambda_filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:signer_impl_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "requires_trusted_downstream_and_upstream",
    status = "alpha",
    deps = [
        ":aws_lambda_filter_lib",
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/aws_lambda/v3:pkg_cc_proto",
    ],
)

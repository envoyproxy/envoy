load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP AWS request signing filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/aws_request_signing_filter

envoy_extension_package()

envoy_cc_library(
    name = "aws_request_signing_filter_lib",
    srcs = ["aws_request_signing_filter.cc"],
    hdrs = ["aws_request_signing_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:region_provider_impl_lib",
        "//source/extensions/common/aws:sigv4_signer_impl_lib",
        "//source/extensions/common/aws:sigv4a_signer_impl_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/aws_request_signing/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":aws_request_signing_filter_lib",
        "//envoy/registry",
        "//source/common/common:matchers_lib",
        "//source/extensions/common/aws:credentials_provider_impl_lib",
        "//source/extensions/common/aws:sigv4_signer_impl_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/aws_request_signing/v3:pkg_cc_proto",
    ],
)

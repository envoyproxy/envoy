load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# HTTP L7 filter that performs oauth.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/oauth2_filter

envoy_cc_library(
    name = "oauth_callback_interface",
    hdrs = ["oauth.h"],
)

envoy_proto_library(
    name = "oauth_response",
    srcs = ["oauth_response.proto"],
)

envoy_cc_library(
    name = "oauth_client",
    srcs = ["oauth_client.cc"],
    hdrs = ["oauth_client.h"],
    deps = [
        ":oauth_response_cc_proto",
        "//envoy/http:async_client_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:base64_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "//source/extensions/filters/http/oauth2:oauth_callback_interface",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "oauth_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":oauth_client",
        "//envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:matchers_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:header_utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/secret:secret_provider_impl_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@com_github_google_jwt_verify//:jwt_verify_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/oauth2/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":oauth_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/oauth2/v3:pkg_cc_proto",
    ],
)

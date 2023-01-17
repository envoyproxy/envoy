load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# HTTP L7 filter that injects credentials.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/credentials_filter

envoy_cc_library(
    name = "injector_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        ":source_lib",
        "//envoy/http:header_map_interface",
        "//envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/credentials/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":injector_lib",
        ":source_lib",
        ":source_basic_auth_lib",
        ":source_bearer_token_lib",
        ":source_oauth2_client_credentials_grant_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/credentials/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "source_lib",
    hdrs = ["source.h"],
    deps = [
        "//source/common/config:datasource_lib",
    ],
)

envoy_cc_library(
    name = "secret_reader_interface",
    hdrs = ["secret_reader.h"],
)

envoy_cc_library(
    name = "source_basic_auth_lib",
    srcs = ["source_basic_auth.cc"],
    hdrs = ["source_basic_auth.h"],
    deps = [
        ":source_lib",
        ":secret_reader_interface",
        "//envoy/api:api_interface",
        "//envoy/secret:secret_provider_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/common:base64_lib",
    ],
)

envoy_cc_library(
    name = "source_bearer_token_lib",
    srcs = ["source_bearer_token.cc"],
    hdrs = ["source_bearer_token.h"],
    deps = [
        ":source_lib",
        ":secret_reader_interface",
        "//envoy/api:api_interface",
        "//envoy/secret:secret_provider_interface",
        "//envoy/thread_local:thread_local_interface",
    ],
)

envoy_cc_library(
    name = "source_oauth2_client_credentials_grant_lib",
    srcs = ["source_oauth2_client_credentials_grant.cc"],
    hdrs = ["source_oauth2_client_credentials_grant.h"],
    deps = [
        ":source_lib",
        ":secret_reader_interface",
        ":oauth_client",
        "//envoy/api:api_interface",
        "//envoy/secret:secret_provider_interface",
        "@envoy_api//envoy/extensions/filters/http/credentials/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "oauth_client",
    srcs = ["oauth_client.cc"],
    hdrs = ["oauth_client.h"],
    deps = [
        ":oauth_callback_interface",
        ":oauth_response_cc_proto",
        "//envoy/http:async_client_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf:message_validator_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "oauth_callback_interface",
    hdrs = ["oauth.h"],
)

envoy_proto_library(
    name = "oauth_response",
    srcs = ["oauth_response.proto"],
)

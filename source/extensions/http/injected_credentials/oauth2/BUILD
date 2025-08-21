load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "token_provider_lib",
    srcs = ["token_provider.cc"],
    hdrs = ["token_provider.h"],
    deps = [
        ":oauth_client",
        "//source/extensions/http/injected_credentials/common:credential_interface",
        "//source/extensions/http/injected_credentials/common:secret_reader_lib",
        "@envoy_api//envoy/extensions/http/injected_credentials/oauth2/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "client_credentials_lib",
    srcs = ["client_credentials_impl.cc"],
    hdrs = ["client_credentials_impl.h"],
    deps = [
        ":token_provider_lib",
        "//source/extensions/http/injected_credentials/common:credential_interface",
        "//source/extensions/http/injected_credentials/common:secret_reader_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":client_credentials_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/http/injected_credentials/common:credential_factory_base_lib",
        "//source/extensions/http/injected_credentials/common:secret_reader_lib",
        "@envoy_api//envoy/extensions/http/injected_credentials/oauth2/v3:pkg_cc_proto",
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

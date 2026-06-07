load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "gcp_authn",
    srcs = ["gcp_authn_filter.cc"],
    hdrs = ["gcp_authn_filter.h"],
    deps = [
        "token_cache",
        ":crypto_utils",
        ":gcp_authn_client_impl",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "gcp_authn_client_interface",
    hdrs = ["gcp_authn_client.h"],
    deps = [
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/types:optional",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "gcp_authn_client_impl",
    srcs = ["gcp_authn_client_impl.cc"],
    hdrs = ["gcp_authn_client_impl.h"],
    deps = [
        ":gcp_authn_client_interface",
        "//source/common/common:utility_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/jwt:jwt_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@abseil-cpp//absl/status:statusor",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "token_cache",
    srcs = ["token_cache.cc"],
    hdrs = ["token_cache.h"],
    deps = [
        ":gcp_authn_client_interface",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/jwt:jwt_lib",
        "//source/common/jwt:simple_lru_cache_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["filter_config.cc"],
    hdrs = ["filter_config.h"],
    deps = [
        ":gcp_authn",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "crypto_utils",
    srcs = ["crypto_utils.cc"],
    hdrs = ["crypto_utils.h"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/secret:secret_provider_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:matchers_lib",
        "//source/common/config:datasource_lib",
        "//source/common/http:utility_lib",
        "//source/common/tls:utility_lib",
        "@abseil-cpp//absl/strings:string_view",
    ],
)

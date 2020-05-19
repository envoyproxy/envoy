licenses(["notice"])  # Apache 2

# HTTP L7 filter that performs oauth.
# Public docs: docs/root/configuration/http_filters/oauth_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "oauth_callback_interface",
    hdrs = ["oauth.h"],
    deps = [
        "//include/envoy/json:json_object_interface",
    ],
)

envoy_cc_library(
    name = "oauth_client",
    srcs = ["oauth_client.cc"],
    hdrs = ["oauth_client.h"],
    deps = [
        "//include/envoy/http:async_client_interface",
        "//include/envoy/json:json_object_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/http:message_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/http/oauth:oauth_callback_interface",
    ],
)

envoy_cc_library(
    name = "oauth_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/config:datasource_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/common/crypto:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/extensions/filters/http/oauth:oauth_client",
        "@envoy_api//envoy/extensions/filters/http/oauth/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/oauth:oauth_lib",
        "@envoy_api//envoy/extensions/filters/http/oauth/v3:pkg_cc_proto",
    ],
)

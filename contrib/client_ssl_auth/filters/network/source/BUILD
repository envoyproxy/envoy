load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# Client SSL authorization L4 network filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/client_ssl_auth_filter

envoy_contrib_package()

envoy_cc_library(
    name = "client_ssl_auth",
    srcs = ["client_ssl_auth.cc"],
    hdrs = ["client_ssl_auth.h"],
    deps = [
        "//envoy/network:connection_interface",
        "//envoy/network:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//envoy/stats:stats_macros",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:headers_lib",
        "//source/common/http:message_lib",
        "//source/common/http:rest_api_fetcher_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/network:utility_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/client_ssl_auth/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":client_ssl_auth",
        "//envoy/registry",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/client_ssl_auth/v3:pkg_cc_proto",
    ],
)

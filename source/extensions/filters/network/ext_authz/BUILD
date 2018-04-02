licenses(["notice"])  # Apache 2
# External authorization L4 network filter
# Public docs: TODO(saumoh): Docs needed in https://github.com/envoyproxy/data-plane-api/tree/master/docs/root/configuration/network_filters

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "ext_authz",
    srcs = ["ext_authz.cc"],
    hdrs = ["ext_authz.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/stats:stats_macros",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:assert_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/filters/common/ext_authz:ext_authz_interface",
        "//source/extensions/filters/common/ext_authz:ext_authz_lib",
        "@envoy_api//envoy/config/filter/network/ext_authz/v2:ext_authz_cc",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:well_known_names",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/ext_authz",
        "@envoy_api//envoy/config/filter/network/ext_authz/v2:ext_authz_cc",
    ],
)

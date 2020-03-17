licenses(["notice"])  # Apache 2

# Direct response L4 network filter.
# Public docs: docs/root/configuration/network_filters/direct_response_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "filter",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    security_posture = "unknown",
    deps = [
        ":filter",
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/config:datasource_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/direct_response/v3:pkg_cc_proto",
    ],
)

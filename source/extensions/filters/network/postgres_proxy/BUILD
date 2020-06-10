load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

#package(default_visibility = ["//visibility:public"])

# PostgresSQL proxy L7 network filter.
# Public docs: docs/root/configuration/network_filters/postgres_proxy_filter.rst

envoy_package()

envoy_cc_library(
    name = "filter",
    srcs = [
        "postgres_decoder.cc",
        "postgres_filter.cc",
    ],
    hdrs = [
        "postgres_decoder.h",
        "postgres_filter.h",
        "postgres_session.h",
    ],
    repository = "@envoy",
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":filter",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/postgres_proxy/v3alpha:pkg_cc_proto",
    ],
)

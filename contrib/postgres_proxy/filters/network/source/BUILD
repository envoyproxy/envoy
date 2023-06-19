load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

#package(default_visibility = ["//visibility:public"])

# PostgresSQL proxy L7 network filter.
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/listeners/network_filters/postgres_proxy_filter

envoy_cc_library(
    name = "filter",
    srcs = [
        "postgres_decoder.cc",
        "postgres_filter.cc",
        "postgres_message.cc",
    ],
    hdrs = [
        "postgres_decoder.h",
        "postgres_filter.h",
        "postgres_message.h",
        "postgres_session.h",
    ],
    repository = "@envoy",
    deps = [
        "//contrib/common/sqlutils/source:sqlutils_lib",
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:filter_lib",
        "//source/extensions/filters/network:well_known_names",
        "@envoy_api//contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":filter",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/postgres_proxy/v3alpha:pkg_cc_proto",
    ],
)

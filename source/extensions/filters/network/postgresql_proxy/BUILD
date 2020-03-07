licenses(["notice"])  # Apache 2

package(default_visibility = ["//visibility:public"])

# PostgresSQL proxy L7 network filter.
# Public docs: docs/root/configuration/network_filters/postgres_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

load("@envoy_api//bazel:api_build_system.bzl", "api_proto_package")

api_proto_package()

envoy_cc_library(
    name = "proxy",
    srcs = ["proxy.cc"],
    hdrs = ["proxy.h"],
    repository = "@envoy",
    deps = [
        "//include/envoy/network:filter_interface",
        "//include/envoy/server:filter_config_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/network:filter_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":proxy",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/postgresql_proxy/v3alpha:pkg_cc_proto",
    ],
)

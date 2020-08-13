licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_package",
)

envoy_extension_package()

envoy_package()

envoy_cc_library(
    name = "downstream_connections_monitor",
    srcs = ["downstream_connections_monitor.cc"],
    hdrs = ["downstream_connections_monitor.h"],
    deps = [
        "//include/envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/memory:stats_lib",
        "@envoy_api//envoy/config/resource_monitor/downstream_connections/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "data_plane_agnostic",
    status = "alpha",
    deps = [
        ":downstream_connections_monitor",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors:well_known_names",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/config/resource_monitor/downstream_connections/v3:pkg_cc_proto",
    ],
)

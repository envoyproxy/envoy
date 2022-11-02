load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "downstream_connections_monitor",
    srcs = ["downstream_connections_monitor.cc"],
    hdrs = ["downstream_connections_monitor.h"],
    deps = [
        "//envoy/server:proactive_resource_monitor_interface",
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/common:thread_synchronizer_lib",
        "@envoy_api//envoy/extensions/resource_monitors/downstream_connections/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":downstream_connections_monitor",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/extensions/resource_monitors/downstream_connections/v3:pkg_cc_proto",
    ],
)

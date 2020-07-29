load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "fixed_heap_monitor",
    srcs = ["fixed_heap_monitor.cc"],
    hdrs = ["fixed_heap_monitor.h"],
    deps = [
        "//include/envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/memory:stats_lib",
        "@envoy_api//envoy/config/resource_monitor/fixed_heap/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "data_plane_agnostic",
    status = "alpha",
    deps = [
        ":fixed_heap_monitor",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors:well_known_names",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/config/resource_monitor/fixed_heap/v2alpha:pkg_cc_proto",
    ],
)

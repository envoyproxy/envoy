licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "unused_heap_monitor",
    srcs = ["unused_heap_monitor.cc"],
    hdrs = ["unused_heap_monitor.h"],
    deps = [
        "//include/envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/memory:stats_lib",
        "//source/extensions/resource_monitors/common:memory_stats_reader_lib",
        "@envoy_api//envoy/config/resource_monitor/unused_heap/v2alpha:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "data_plane_agnostic",
    status = "alpha",
    deps = [
        ":unused_heap_monitor",
        "//include/envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors:well_known_names",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/config/resource_monitor/unused_heap/v2alpha:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cgroup_memory_monitor",
    srcs = [
        "cgroup_memory_monitor.cc",
        "cgroup_memory_stats_reader.cc",
    ],
    hdrs = [
        "cgroup_memory_monitor.h",
        "cgroup_memory_stats_reader.h",
        "cgroup_memory_paths.h",
    ],
    deps = [
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
        "//envoy/common:exception_lib", 
        "@envoy_api//envoy/extensions/resource_monitors/cgroup_memory/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cgroup_memory_monitor",
        "//envoy/registry",
        "//source/common/common:assert_lib",
        "//source/extensions/resource_monitors/common:factory_base_lib",
        "@envoy_api//envoy/extensions/resource_monitors/cgroup_memory/v3:pkg_cc_proto",
    ],
)
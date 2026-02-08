load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cgroup_memory_paths",
    hdrs = ["cgroup_memory_paths.h"],
    deps = [
        "//envoy/common:pure_lib",
        "//envoy/filesystem:filesystem_interface",
        "//source/common/common:assert_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "cgroup_memory_stats_reader",
    srcs = ["cgroup_memory_stats_reader.cc"],
    hdrs = ["cgroup_memory_stats_reader.h"],
    deps = [
        ":cgroup_memory_paths",
        "//envoy/common:exception_lib",
        "//envoy/filesystem:filesystem_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
        "//source/common/common:thread_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "cgroup_memory_monitor",
    srcs = ["cgroup_memory_monitor.cc"],
    hdrs = ["cgroup_memory_monitor.h"],
    deps = [
        ":cgroup_memory_stats_reader",
        "//envoy/common:exception_lib",
        "//envoy/filesystem:filesystem_interface",
        "//envoy/server:resource_monitor_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
        "//source/common/common:thread_lib",
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

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.cgroup_memory"],
    deps = [
        "//envoy/registry",
        "//source/extensions/resource_monitors/cgroup_memory:cgroup_memory_monitor",
        "//source/extensions/resource_monitors/cgroup_memory:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cgroup_memory_monitor_test",
    srcs = ["cgroup_memory_monitor_test.cc"],
    extension_names = ["envoy.resource_monitors.cgroup_memory"],
    deps = [
        "//source/extensions/resource_monitors/cgroup_memory:cgroup_memory_monitor",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "cgroup_memory_paths_test",
    srcs = ["cgroup_memory_paths_test.cc"],
    extension_names = ["envoy.resource_monitors.cgroup_memory"],
    deps = [
        "//source/extensions/resource_monitors/cgroup_memory:cgroup_memory_paths",
    ],
)

envoy_extension_cc_test(
    name = "cgroup_memory_stats_reader_test",
    srcs = ["cgroup_memory_stats_reader_test.cc"],
    extension_names = ["envoy.resource_monitors.cgroup_memory"],
    deps = [
        "//source/extensions/resource_monitors/cgroup_memory:cgroup_memory_monitor",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

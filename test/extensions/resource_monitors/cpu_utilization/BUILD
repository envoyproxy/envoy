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
    name = "cpu_utilization_monitor_test",
    srcs = ["cpu_utilization_monitor_test.cc"],
    extension_names = ["envoy.resource_monitors.cpu_utilization"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/resource_monitors/cpu_utilization:cpu_utilization_monitor",
        "//source/extensions/resource_monitors/cpu_utilization:linux_cpu_stats_reader",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/resource_monitors/cpu_utilization/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "linux_cpu_stats_reader_test",
    srcs = ["linux_cpu_stats_reader_test.cc"],
    extension_names = ["envoy.resource_monitors.cpu_utilization"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/resource_monitors/cpu_utilization:linux_cpu_stats_reader",
        "//test/test_common:environment_lib",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.cpu_utilization"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/registry",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/resource_monitors/cpu_utilization:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:options_mocks",
        "@envoy_api//envoy/extensions/resource_monitors/cpu_utilization/v3:pkg_cc_proto",
    ],
)

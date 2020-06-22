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
    name = "fixed_heap_monitor_test",
    srcs = ["fixed_heap_monitor_test.cc"],
    extension_name = "envoy.resource_monitors.fixed_heap",
    external_deps = ["abseil_optional"],
    deps = [
        "//source/extensions/resource_monitors/fixed_heap:fixed_heap_monitor",
        "@envoy_api//envoy/config/resource_monitor/fixed_heap/v2alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.resource_monitors.fixed_heap",
    deps = [
        "//include/envoy/registry",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/resource_monitors/fixed_heap:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "@envoy_api//envoy/config/resource_monitor/fixed_heap/v2alpha:pkg_cc_proto",
    ],
)

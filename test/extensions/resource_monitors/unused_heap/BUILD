licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "unused_heap_monitor_test",
    srcs = ["unused_heap_monitor_test.cc"],
    extension_name = "envoy.resource_monitors.unused_heap",
    external_deps = ["abseil_optional"],
    deps = [
        "//source/extensions/resource_monitors/unused_heap:unused_heap_monitor",
        "//test/extensions/resource_monitors:resource_monitor_mocks",
        "@envoy_api//envoy/config/resource_monitor/unused_heap/v2alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.resource_monitors.unused_heap",
    deps = [
        "//include/envoy/registry",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/resource_monitors/unused_heap:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "@envoy_api//envoy/config/resource_monitor/unused_heap/v2alpha:pkg_cc_proto",
    ],
)

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
    extension_names = ["envoy.resource_monitors.fixed_heap"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/resource_monitors/fixed_heap:fixed_heap_monitor",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/resource_monitors/fixed_heap/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.fixed_heap"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/resource_monitors/fixed_heap:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:options_mocks",
        "@envoy_api//envoy/extensions/resource_monitors/fixed_heap/v3:pkg_cc_proto",
    ],
)

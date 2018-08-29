licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_cc_test",
)

envoy_package()

envoy_cc_test(
    name = "injected_resource_monitor_test",
    srcs = ["injected_resource_monitor_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/extensions/resource_monitors/injected_resource:injected_resource_monitor",
        "//source/server:resource_monitor_config_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//include/envoy/registry",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/resource_monitors/injected_resource:config",
        "//source/server:resource_monitor_config_lib",
        "//test/test_common:test_time_lib",
        "@envoy_api//envoy/config/resource_monitor/injected_resource/v2alpha:injected_resource_cc",
    ],
)

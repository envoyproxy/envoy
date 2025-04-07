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
        "//source/extensions/resource_monitors/cgroup_memory:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:server_mocks",  # Added this dependency
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/resource_monitors/cgroup_memory/v3:pkg_cc_proto",
    ],
)
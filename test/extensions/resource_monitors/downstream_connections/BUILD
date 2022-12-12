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
    name = "downstream_connections_monitor_test",
    srcs = ["downstream_connections_monitor_test.cc"],
    extension_names = ["envoy.resource_monitors.downstream_connections"],
    external_deps = ["abseil_optional"],
    deps = [
        "//source/extensions/resource_monitors/downstream_connections:downstream_connections_monitor",
        "@envoy_api//envoy/extensions/resource_monitors/downstream_connections/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.downstream_connections"],
    deps = [
        "//envoy/registry",
        "//source/extensions/resource_monitors/downstream_connections:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:options_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/resource_monitors/downstream_connections/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
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
    extension_names = ["envoy.resource_monitors.global_downstream_max_connections"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/resource_monitors/downstream_connections:downstream_connections_monitor",
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/resource_monitors/downstream_connections/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.global_downstream_max_connections"],
    rbe_pool = "2core",
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

envoy_cc_test(
    name = "cx_limit_overload_integration_test",
    size = "large",
    srcs = ["cx_limit_overload_integration_test.cc"],
    rbe_pool = "2core",
    tags = [
        "cpu:3",
    ],
    deps = [
        "//envoy/network:filter_interface",
        "//envoy/registry",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/resource_monitors/downstream_connections:config",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

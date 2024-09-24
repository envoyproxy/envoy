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

envoy_cc_test(
    name = "injected_resource_monitor_integration_test",
    size = "large",
    srcs = ["injected_resource_monitor_integration_test.cc"],
    rbe_pool = "2core",
    # Broken until bazel 5.0.0 fix to shorten resulting paths for SymInitialize() failure
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/resource_monitors/injected_resource:config",
        "//test/common/config:dummy_config_proto_cc_proto",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "injected_resource_monitor_test",
    srcs = ["injected_resource_monitor_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/resource_monitors/injected_resource:injected_resource_monitor",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/resource_monitors/injected_resource/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.injected_resource"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/resource_monitors/injected_resource:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/extensions/resource_monitors/injected_resource/v3:pkg_cc_proto",
    ],
)

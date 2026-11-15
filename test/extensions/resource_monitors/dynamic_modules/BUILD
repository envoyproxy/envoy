load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_dyn_module_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_missing_update_resource_usage",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_no_op",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_synchronous_feedback",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_synchronous_feedback_missing_get_resource_usage",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_synchronous_feedback_missing_load_accepted",
        "//test/extensions/dynamic_modules/test_data/rust:resource_monitor_test",
    ],
    extension_names = ["envoy.resource_monitors.dynamic_modules"],
    deps = [
        "//envoy/registry",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/resource_monitors/dynamic_modules:config",
        "//source/server:resource_monitor_config_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:options_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/resource_monitors/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "resource_monitor_test",
    srcs = ["resource_monitor_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_no_op",
        "//test/extensions/dynamic_modules/test_data/c:resource_monitor_synchronous_feedback",
    ],
    extension_names = ["envoy.resource_monitors.dynamic_modules"],
    deps = [
        "//source/extensions/resource_monitors/dynamic_modules:abi_impl",
        "//source/extensions/resource_monitors/dynamic_modules:resource_monitor_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/event:event_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:resource_monitor_test",
    ],
    extension_names = ["envoy.resource_monitors.dynamic_modules"],
    rbe_pool = {
        "tsan": {"build": "linux_x64_small"},
        "gcc": {"build": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/resource_monitors/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

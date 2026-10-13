load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "dynamic_module_memory_monitor_test",
    srcs = ["dynamic_module_memory_monitor_test.cc"],
    data = ["//test/extensions/dynamic_modules/test_data/c:memory_allocator"],
    extension_names = ["envoy.resource_monitors.dynamic_module_memory"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "//source/extensions/resource_monitors/dynamic_module_memory:dynamic_module_memory_monitor",
        "//test/extensions/dynamic_modules:util",
        "@envoy_api//envoy/extensions/resource_monitors/dynamic_module_memory/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.resource_monitors.dynamic_module_memory"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//envoy/registry",
        "//source/extensions/resource_monitors/dynamic_module_memory:config",
        "//source/server:resource_monitor_config_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:options_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/resource_monitors/dynamic_module_memory/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:memory_integration_test",
        "//test/extensions/dynamic_modules/test_data/rust:memory_tracking_integration_test",
    ],
    extension_names = ["envoy.resource_monitors.dynamic_module_memory"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/filters/http/dynamic_modules:abi_impl",
        "//source/extensions/filters/http/dynamic_modules:factory_registration",
        "//source/extensions/resource_monitors/dynamic_module_memory:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/overload/v3:pkg_cc_proto",
    ],
)

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
        "//test/extensions/dynamic_modules/test_data/c:lb_callbacks_test",
        "//test/extensions/dynamic_modules/test_data/c:lb_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:lb_invalid_host_index",
        "//test/extensions/dynamic_modules/test_data/c:lb_invalid_priority",
        "//test/extensions/dynamic_modules/test_data/c:lb_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:lb_no_choose_host",
        "//test/extensions/dynamic_modules/test_data/c:lb_round_robin",
        "//test/extensions/dynamic_modules/test_data/c:lb_server_initialized_no_schedule",
        "//test/extensions/dynamic_modules/test_data/c:lb_server_initialized_test",
    ],
    extension_names = ["envoy.load_balancing_policies.dynamic_modules"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/common/upstream:upstream_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/load_balancing_policies/dynamic_modules:config",
        "//source/extensions/load_balancing_policies/dynamic_modules:load_balancer_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/init:init_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:lb_callout_timer",
        "//test/extensions/dynamic_modules/test_data/c:lb_round_robin",
    ],
    extension_names = ["envoy.load_balancing_policies.dynamic_modules"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/load_balancing_policies/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:lb_integration_test",
    ],
    extension_names = ["envoy.load_balancing_policies.dynamic_modules"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/load_balancing_policies/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/load_balancing_policies/dynamic_modules/v3:pkg_cc_proto",
    ],
)

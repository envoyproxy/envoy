load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:udp_no_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:udp_no_filter_destroy",
        "//test/extensions/dynamic_modules/test_data/c:udp_no_filter_new",
        "//test/extensions/dynamic_modules/test_data/c:udp_no_on_data",
        "//test/extensions/dynamic_modules/test_data/c:udp_no_op",
        "//test/extensions/dynamic_modules/test_data/c:udp_stop_iteration",
    ],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/filters/udp/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:udp_no_op",
    ],
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/udp/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:no_op",
        "//test/extensions/dynamic_modules/test_data/c:udp_no_op",
    ],
    deps = [
        "//source/extensions/filters/udp/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "udp_dynamic_modules_integration_test",
    srcs = ["udp_dynamic_modules_integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:udp_no_op",
        "//test/extensions/dynamic_modules/test_data/c:udp_stop_iteration",
    ],
    deps = [
        "//source/extensions/filters/udp/dynamic_modules:config",
        "//source/extensions/filters/udp/udp_proxy:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:integration_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/udp_proxy/v3:pkg_cc_proto",
    ],
)

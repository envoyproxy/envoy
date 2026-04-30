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
        "//test/extensions/dynamic_modules/test_data/c:listener_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:listener_filter_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:listener_no_op",
        "//test/extensions/dynamic_modules/test_data/c:listener_stop_iteration",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/listener/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:listener_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:listener_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stats:custom_stat_namespaces_lib",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:listener_factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/listener/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        # Rust variant disabled — the Rust SDK is missing listener filter ABI
        # exports. See integration_test.cc::getListenerTestParams.
        "//test/extensions/dynamic_modules/test_data/go:listener_integration_test",
    ],
    env = {"GODEBUG": "cgocheck=0"},
    rbe_pool = "6gig",
    deps = [
        # See //test/extensions/dynamic_modules/http:filter_test for why every test
        # binary that loads a Go test_data .so depends on all_abi_impls.
        "//source/extensions/dynamic_modules:all_abi_impls",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/dynamic_modules/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:listener_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/http:message_lib",
        "//source/common/network:address_lib",
        "//source/common/network:io_socket_error_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/listener/dynamic_modules:config",
        "//source/extensions/filters/listener/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/tracing:tracing_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

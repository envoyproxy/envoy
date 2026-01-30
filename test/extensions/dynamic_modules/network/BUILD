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
        "//test/extensions/dynamic_modules/test_data/c:network_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:network_filter_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:network_no_op",
        "//test/extensions/dynamic_modules/test_data/c:network_stop_iteration",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/network/dynamic_modules:config",
        "//source/extensions/filters/network/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test(
    name = "factory_test",
    srcs = ["factory_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:network_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:network_no_op",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/network/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/network/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:network_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/filters/network/dynamic_modules:config",
        "//source/extensions/filters/network/dynamic_modules:filter_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
    ],
)

envoy_cc_test(
    name = "integration_test",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:network_no_op",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/network/dynamic_modules:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/extensions/filters/network/dynamic_modules/v3:pkg_cc_proto",
    ],
)

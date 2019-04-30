licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_binary",
)

envoy_package()

envoy_cc_mock(
    name = "multiplexing_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/common/multiplexing:conn_pool_interface",
        "//source/extensions/filters/network/common/multiplexing:router_interface",
    ],
)

envoy_extension_cc_test(
    name = "conn_pool_impl_test",
    srcs = ["conn_pool_impl_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":multiplexing_mocks",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/common/multiplexing:conn_pool_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/extensions/filters/network/common/redis:test_utils_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "router_impl_test",
    srcs = ["router_impl_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":multiplexing_mocks",
        "//source/extensions/filters/network/common/multiplexing:router_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/test_common:utility_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_platform_dep",
    "envoy_cc_posix_library",
    "envoy_cc_win32_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_binary",
)

envoy_package()

envoy_extension_cc_test(
    name = "command_splitter_impl_test",
    srcs = ["command_splitter_impl_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":redis_mocks",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//source/extensions/filters/network/redis_proxy:router_interface",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks:common_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "conn_pool_impl_test",
    srcs = ["conn_pool_impl_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":redis_mocks",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/common/redis:utility_lib",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "//test/extensions/clusters/redis:redis_cluster_mocks",
        "//test/extensions/common/redis:mocks_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/extensions/filters/network/common/redis:test_utils_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":redis_mocks",
        "//source/common/config:json_utility_lib",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/filters/network/redis_proxy:proxy_filter_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks:common_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_mock(
    name = "redis_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/extensions/common/redis:redirection_mgr_interface",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_interface",
        "//source/extensions/filters/network/redis_proxy:conn_pool_interface",
        "//source/extensions/filters/network/redis_proxy:router_interface",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test_binary(
    name = "command_lookup_speed_test",
    srcs = ["command_lookup_speed_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    external_deps = [
        "benchmark",
    ],
    deps = [
        ":redis_mocks",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "router_impl_test",
    srcs = ["router_impl_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":redis_mocks",
        "//source/extensions/filters/network/redis_proxy:router_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "redis_proxy_integration_test",
    srcs = ["redis_proxy_integration_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/integration:integration_lib",
    ],
)

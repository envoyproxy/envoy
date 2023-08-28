load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_benchmark_test",
    "envoy_extension_cc_benchmark_binary",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "command_splitter_impl_test",
    srcs = ["command_splitter_impl_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        ":redis_mocks",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/common/redis:fault_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//source/extensions/filters/network/redis_proxy:router_interface",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks:common_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
    ],
)

envoy_extension_cc_test(
    name = "conn_pool_impl_test",
    srcs = ["conn_pool_impl_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        ":redis_mocks",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/common/redis:utility_lib",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "//test/extensions/clusters/redis:redis_cluster_mocks",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/extensions/common/redis:mocks_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/extensions/filters/network/common/redis:test_utils_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:cluster_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_handle_mocks",
        "//test/mocks/upstream:cluster_update_callbacks_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:host_set_mocks",
        "//test/mocks/upstream:thread_local_cluster_mocks",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/redis/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        ":redis_mocks",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/filters/network/redis_proxy:proxy_filter_lib",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks:common_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_mock(
    name = "redis_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/common/redis:cluster_refresh_manager_interface",
        "//source/extensions/filters/network/common/redis:client_interface",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/common/redis:fault_interface",
        "//source/extensions/filters/network/redis_proxy:command_splitter_interface",
        "//source/extensions/filters/network/redis_proxy:conn_pool_interface",
        "//source/extensions/filters/network/redis_proxy:router_interface",
        "//test/test_common:printers_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_benchmark_binary(
    name = "command_lookup_speed_test",
    srcs = ["command_lookup_speed_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        ":redis_mocks",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_benchmark_test(
    name = "command_lookup_speed_test_benchmark_test",
    benchmark_binary = "command_lookup_speed_test",
    extension_names = ["envoy.filters.network.redis_proxy"],
)

envoy_extension_cc_test(
    name = "router_impl_test",
    srcs = ["router_impl_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        ":redis_mocks",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/filters/network/redis_proxy:router_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks/network:connection_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/redis_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "redis_proxy_integration_test",
    size = "large",
    srcs = ["redis_proxy_integration_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    deps = [
        "//source/extensions/filters/network/common/redis:fault_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/integration:integration_lib",
    ],
)

envoy_extension_cc_benchmark_binary(
    name = "command_split_speed_test",
    srcs = ["command_split_speed_test.cc"],
    extension_names = ["envoy.filters.network.redis_proxy"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        ":redis_mocks",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//source/extensions/filters/network/redis_proxy:router_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_benchmark_test(
    name = "command_split_speed_test_benchmark_test",
    benchmark_binary = "command_split_speed_test",
    extension_names = ["envoy.filters.network.redis_proxy"],
)

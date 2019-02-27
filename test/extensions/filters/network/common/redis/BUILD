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

envoy_extension_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    extension_name = "envoy.filters.network.common.redis",
    deps = [
        #        ":redis_mocks",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "command_splitter_impl_test",
    srcs = ["command_splitter_impl_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        ":redis_mocks",
        "//source/common/stats:isolated_store_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/common/redis:command_splitter_lib",
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
        "//source/extensions/filters/network/common/redis:conn_pool_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_mock(
    name = "redis_mocks",
    srcs = ["mocks.cc"],
    hdrs = ["mocks.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/extensions/filters/network/common/redis:codec_lib",
        "//source/extensions/filters/network/common/redis:command_splitter_interface",
        "//source/extensions/filters/network/common/redis:conn_pool_interface",
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
        "//source/extensions/filters/network/common/redis:command_splitter_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:simulated_time_system_lib",
    ],
)

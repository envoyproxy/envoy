licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    deps = [
        ":redis_mocks",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/filters/network/redis_proxy:codec_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "command_splitter_impl_test",
    srcs = ["command_splitter_impl_test.cc"],
    deps = [
        ":redis_mocks",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_lib",
        "//test/mocks:common_lib",
    ],
)

envoy_cc_test(
    name = "conn_pool_impl_test",
    srcs = ["conn_pool_impl_test.cc"],
    deps = [
        ":redis_mocks",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/filters/network/redis_proxy:conn_pool_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    deps = [
        ":redis_mocks",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
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
        "//source/extensions/filters/network/redis_proxy:codec_lib",
        "//source/extensions/filters/network/redis_proxy:command_splitter_interface",
        "//source/extensions/filters/network/redis_proxy:conn_pool_interface",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/redis:codec_lib",
        "//test/mocks/redis:redis_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "command_splitter_impl_test",
    srcs = ["command_splitter_impl_test.cc"],
    deps = [
        "//source/common/redis:command_splitter_lib",
        "//source/common/stats:stats_lib",
        "//test/mocks:common_lib",
        "//test/mocks/redis:redis_mocks",
    ],
)

envoy_cc_test(
    name = "conn_pool_impl_test",
    srcs = ["conn_pool_impl_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/redis:conn_pool_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/redis:redis_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    deps = [
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/redis:proxy_filter_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/redis:redis_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "conn_pool_test",
    srcs = ["conn_pool_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/tcp:conn_pool_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "async_tcp_client_impl_test",
    srcs = ["async_tcp_client_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:utility_lib",
        "//source/common/tcp:async_tcp_client_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

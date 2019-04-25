licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "redis_cluster_test",
    srcs = ["redis_cluster_test.cc"],
    extension_name = "envoy.clusters.redis",
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/clusters/redis:redis_cluster",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/server:transport_socket_config_lib",
        "//test/common/upstream:utility_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/extensions/filters/network/common/redis:test_utils_lib",
        "//test/extensions/filters/network/redis_proxy:redis_mocks",
        "//test/mocks:common_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/cluster/redis:redis_cluster_cc",
    ],
)

envoy_extension_cc_test(
    name = "redis_cluster_integration_test",
    size = "small",
    srcs = ["redis_cluster_integration_test.cc"],
    extension_name = "envoy.clusters.redis",
    deps = [
        "//source/extensions/clusters/redis:redis_cluster",
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/integration:integration_lib",
    ],
)

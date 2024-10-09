load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "statsd_test",
    srcs = ["statsd_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/stat_sinks/common/statsd:statsd_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:host_mocks",
    ],
)

envoy_cc_test(
    name = "udp_statsd_test",
    srcs = ["udp_statsd_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/stat_sinks/common/statsd:statsd_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

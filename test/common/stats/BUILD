licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "stats_impl_test",
    srcs = ["stats_impl_test.cc"],
    deps = [
        "//source/common/stats:stats_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/metrics/v2:stats_cc",
    ],
)

envoy_cc_test(
    name = "statsd_test",
    srcs = ["statsd_test.cc"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:statsd_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "metrics_service_test",
    srcs = ["grpc_metrics_service_impl_test.cc"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:metrics_service_grpc_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "udp_statsd_test",
    srcs = ["udp_statsd_test.cc"],
    deps = [
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/stats:statsd_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
    ],
)

envoy_cc_test(
    name = "thread_local_store_test",
    srcs = ["thread_local_store_test.cc"],
    deps = [
        "//source/common/stats:thread_local_store_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:utility_lib",
    ],
)

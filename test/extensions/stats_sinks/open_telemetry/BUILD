load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "metric_aggregator_test",
    srcs = ["metric_aggregator_test.cc"],
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:metric_aggregator_lib",
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "open_telemetry_impl_test",
    srcs = ["open_telemetry_impl_test.cc"],
    deps = [
        "//source/common/stats:histogram_lib",
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "open_telemetry_http_impl_test",
    srcs = ["open_telemetry_http_impl_test.cc"],
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "open_telemetry_integration_test",
    srcs = ["open_telemetry_integration_test.cc"],
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
        
    ],
)

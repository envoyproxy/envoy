load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "metrics_service_test",
    srcs = ["grpc_metrics_service_impl_test.cc"],
    extension_names = ["envoy.stat_sinks.metrics_service"],
    rbe_pool = "2core",
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/stat_sinks/metrics_service:metrics_service_grpc_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "@envoy_api//envoy/service/metrics/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "metrics_service_integration_test",
    size = "large",
    srcs = ["metrics_service_integration_test.cc"],
    extension_names = ["envoy.stat_sinks.metrics_service"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/stats:histogram_lib",
        "//source/extensions/stat_sinks/metrics_service:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/metrics/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/metrics/v3:pkg_cc_proto",
    ],
)

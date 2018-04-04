licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "metrics_service_test",
    srcs = ["grpc_metrics_service_impl_test.cc"],
    deps = [
        "//source/common/config:well_known_names",
        "//source/common/event:dispatcher_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/stat_sinks/metrics_service:metrics_service_grpc_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_cc_test(
    name = "metrics_service_integration_test",
    srcs = ["metrics_service_integration_test.cc"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/stat_sinks/metrics_service:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/service/metrics/v2:metrics_service_cc",
    ],
)

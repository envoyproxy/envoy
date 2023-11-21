load("@envoy//bazel:envoy_build_system.bzl", "envoy_mobile_package")
load(
    "@envoy//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_extension_cc_test(
    name = "mobile_grpc_streamer_test",
    srcs = ["mobile_grpc_streamer_test.cc"],
    extension_names = ["envoy.stat_sinks.metrics_service.mobile"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/stat_sinks/metrics_service:config",
        "//library/common/extensions/stat_sinks/metrics_service:service_cc_proto",
        "@envoy//test/mocks/grpc:grpc_mocks",
        "@envoy//test/mocks/local_info:local_info_mocks",
        "@envoy//test/mocks/thread_local:thread_local_mocks",
        "@envoy//test/test_common:logging_lib",
    ],
)

envoy_extension_cc_test(
    name = "mobile_grpc_streamer_integration_test",
    srcs = ["mobile_grpc_streamer_integration_test.cc"],
    extension_names = ["envoy.stat_sinks.metrics_service.mobile"],
    repository = "@envoy",
    deps = [
        "//library/common/extensions/stat_sinks/metrics_service:config",
        "//library/common/extensions/stat_sinks/metrics_service:config_proto_cc_proto",
        "//library/common/extensions/stat_sinks/metrics_service:service_cc_proto",
        "@envoy//test/integration:http_integration_lib",
        "@envoy//test/mocks/grpc:grpc_mocks",
        "@envoy//test/mocks/local_info:local_info_mocks",
        "@envoy//test/mocks/thread_local:thread_local_mocks",
        "@envoy//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

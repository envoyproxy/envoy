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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//envoy/registry",
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "open_telemetry_test",
    srcs = ["open_telemetry_impl_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/stat_sinks/open_telemetry:open_telemetry_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/test_common:simulated_time_system_lib",
    ],
)

envoy_extension_cc_test(
    name = "open_telemetry_integration_test",
    size = "large",
    srcs = ["open_telemetry_integration_test.cc"],
    extension_names = ["envoy.stat_sinks.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/common/stats:histogram_lib",
        "//source/extensions/stat_sinks/open_telemetry:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# TODO(itamarkam): Make this an envoy_cc_extension_test once the API is defined.
envoy_cc_test(
    name = "grpc_access_log_impl_test",
    srcs = ["grpc_access_log_impl_test.cc"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/extensions/access_loggers/open_telemetry:grpc_access_log_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)

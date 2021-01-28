load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    # "envoy_extension_cc_test",
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

envoy_cc_test(
    name = "access_log_impl_test",
    srcs = ["access_log_impl_test.cc"],
    # extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/common/protobuf",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/access_loggers/open_telemetry:access_log_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)

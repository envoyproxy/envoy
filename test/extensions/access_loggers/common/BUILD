load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "access_log_base_test",
    srcs = ["access_log_base_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/access_loggers/common:access_log_base",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_cc_test(
    name = "grpc_access_logger_test",
    srcs = ["grpc_access_logger_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/protobuf",
        "//source/extensions/access_loggers/common:grpc_access_logger",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

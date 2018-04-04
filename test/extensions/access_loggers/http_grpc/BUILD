licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "grpc_access_log_impl_test",
    srcs = ["grpc_access_log_impl_test.cc"],
    deps = [
        "//source/extensions/access_loggers/http_grpc:grpc_access_log_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/request_info:request_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/access_loggers/http_grpc:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_cc_test(
    name = "grpc_access_log_integration_test",
    srcs = ["grpc_access_log_integration_test.cc"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/access_loggers/http_grpc:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "grpc_access_log_impl_test",
    srcs = ["grpc_access_log_impl_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/extensions/access_loggers/http_grpc:grpc_access_log_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/request_info:request_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/extensions/access_loggers/http_grpc:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "grpc_access_log_integration_test",
    srcs = ["grpc_access_log_integration_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/access_loggers/http_grpc:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
    ],
)

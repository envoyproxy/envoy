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
    name = "grpc_access_log_impl_test",
    srcs = ["grpc_access_log_impl_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/extensions/access_loggers/grpc:http_grpc_access_log_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "grpc_access_log_utils_test",
    srcs = ["grpc_access_log_utils_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/extensions/access_loggers/grpc:grpc_access_log_utils",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "http_grpc_access_log_impl_test",
    srcs = ["http_grpc_access_log_impl_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/access_loggers/grpc:http_grpc_access_log_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "http_config_test",
    srcs = ["http_config_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    deps = [
        "//source/extensions/access_loggers/grpc:http_config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "http_grpc_access_log_integration_test",
    srcs = ["http_grpc_access_log_integration_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/access_loggers/grpc:http_config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "tcp_grpc_access_log_integration_test",
    srcs = ["tcp_grpc_access_log_integration_test.cc"],
    extension_name = "envoy.access_loggers.http_grpc",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/access_loggers/grpc:http_config",
        "//source/extensions/access_loggers/grpc:tcp_config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

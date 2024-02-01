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
    extension_names = ["envoy.access_loggers.http_grpc"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/extensions/access_loggers/grpc:http_grpc_access_log_lib",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "grpc_access_log_utils_test",
    srcs = ["grpc_access_log_utils_test.cc"],
    extension_names = ["envoy.access_loggers.http_grpc"],
    deps = [
        "//source/extensions/access_loggers/grpc:grpc_access_log_utils",
        "//source/extensions/filters/common/expr:cel_state_lib",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "http_grpc_access_log_impl_test",
    srcs = ["http_grpc_access_log_impl_test.cc"],
    extension_names = ["envoy.access_loggers.http_grpc"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:uint32_accessor_lib",
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
    extension_names = ["envoy.access_loggers.http_grpc"],
    deps = [
        "//source/extensions/access_loggers/grpc:http_config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "http_grpc_access_log_integration_test",
    size = "large",
    srcs = ["http_grpc_access_log_integration_test.cc"],
    extension_names = ["envoy.access_loggers.http_grpc"],
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
    name = "tcp_config_test",
    srcs = ["tcp_config_test.cc"],
    extension_names = ["envoy.access_loggers.tcp_grpc"],
    deps = [
        "//source/extensions/access_loggers/grpc:tcp_config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "tcp_grpc_access_log_integration_test",
    size = "large",
    srcs = ["tcp_grpc_access_log_integration_test.cc"],
    extension_names = ["envoy.access_loggers.tcp_grpc"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/access_loggers/grpc:http_config",
        "//source/extensions/access_loggers/grpc:tcp_config",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/listener/tls_inspector:tls_inspector_lib",
        "//source/extensions/filters/network/echo:config",
        "//source/extensions/filters/network/rbac:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "//source/extensions/transport_sockets/tls:context_lib",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/echo/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/accesslog/v3:pkg_cc_proto",
    ],
)

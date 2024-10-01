load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
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
    extension_names = ["envoy.access_loggers.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/protobuf",
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

envoy_extension_cc_test(
    name = "access_log_impl_test",
    srcs = ["access_log_impl_test.cc"],
    extension_names = ["envoy.access_loggers.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//envoy/common:time_interface",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/protobuf",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/access_loggers/open_telemetry:access_log_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/grpc:grpc_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.access_loggers.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/access_loggers/open_telemetry:access_log_lib",
        "//source/extensions/access_loggers/open_telemetry:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "access_log_integration_test",
    size = "large",
    srcs = ["access_log_integration_test.cc"],
    extension_names = ["envoy.access_loggers.open_telemetry"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/grpc:codec_lib",
        "//source/common/grpc:common_lib",
        "//source/extensions/access_loggers/open_telemetry:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "substitution_formatter_test",
    srcs = ["substitution_formatter_test.cc"],
    copts = select({
        "//bazel:windows_x86_64": [],  # TODO: fix the windows ANTLR build
        "//conditions:default": [
            "-DUSE_CEL_PARSER",
        ],
    }),
    extension_names = ["envoy.access_loggers.open_telemetry"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/http:exception_lib",
        "//source/common/http:header_map_lib",
        "//source/common/router:string_accessor_lib",
        "//source/extensions/access_loggers/open_telemetry:config",
        "//source/extensions/access_loggers/open_telemetry:substitution_formatter_lib",
        "//source/extensions/formatter/cel:cel_lib",
        "//source/extensions/formatter/cel:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/test_common:utility_lib",
        "@opentelemetry_proto//:logs_cc_proto",
    ] + select(
        {
            "//bazel:windows_x86_64": [],
            "//conditions:default": [
                "@com_google_cel_cpp//parser",
            ],
        },
    ),
)

envoy_cc_benchmark_binary(
    name = "substitution_formatter_speed_test",
    srcs = ["substitution_formatter_speed_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/common/http:header_map_lib",
        "//source/common/network:address_lib",
        "//source/extensions/access_loggers/open_telemetry:substitution_formatter_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/stream_info:stream_info_mocks",
        "@com_github_google_benchmark//:benchmark",
        "@opentelemetry_proto//:common_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "substitution_formatter_speed_test_benchmark_test",
    benchmark_binary = "substitution_formatter_speed_test",
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "grpc_access_log_lib",
    srcs = ["grpc_access_log_impl.cc"],
    hdrs = ["grpc_access_log_impl.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/common:grpc_access_logger",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)

envoy_cc_library(
    name = "access_log_lib",
    srcs = ["access_log_impl.cc"],
    hdrs = ["access_log_impl.h"],
    deps = [
        ":grpc_access_log_lib",
        ":substitution_formatter_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry_proto//:logs_cc_proto",
    ],
)

envoy_cc_library(
    name = "access_log_proto_descriptors_lib",
    srcs = ["access_log_proto_descriptors.cc"],
    hdrs = ["access_log_proto_descriptors.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/access_log:access_log_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/open_telemetry:access_log_lib",
        "//source/extensions/access_loggers/open_telemetry:access_log_proto_descriptors_lib",
        "//source/extensions/access_loggers/open_telemetry:grpc_access_log_lib",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "substitution_formatter_lib",
    srcs = ["substitution_formatter.cc"],
    hdrs = ["substitution_formatter.h"],
    external_deps = ["abseil_str_format"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "@opentelemetry_proto//:common_cc_proto",
    ],
)

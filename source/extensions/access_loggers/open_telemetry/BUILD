load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "otlp_log_utils_lib",
    srcs = ["otlp_log_utils.cc"],
    hdrs = ["otlp_log_utils.h"],
    deps = [
        "//envoy/formatter:http_formatter_context_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/stats:stats_macros",
        "//envoy/stream_info:filter_state_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:custom_tag_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:trace_context_lib",
        "//source/common/version:version_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry-proto//:common_proto_cc",
        "@opentelemetry-proto//:logs_proto_cc",
        "@opentelemetry-proto//:logs_service_proto_cc",
    ],
)

envoy_cc_library(
    name = "grpc_access_log_lib",
    srcs = ["grpc_access_log_impl.cc"],
    hdrs = ["grpc_access_log_impl.h"],
    deps = [
        ":otlp_log_utils_lib",
        "//envoy/event:dispatcher_interface",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/common:grpc_access_logger",
        "//source/extensions/access_loggers/common:grpc_access_logger_clients_lib",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry-proto//:logs_proto_cc",
        "@opentelemetry-proto//:logs_service_proto_cc",
    ],
)

envoy_cc_library(
    name = "access_log_lib",
    srcs = ["access_log_impl.cc"],
    hdrs = ["access_log_impl.h"],
    deps = [
        ":grpc_access_log_lib",
        ":otlp_log_utils_lib",
        ":substitution_formatter_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/protobuf:message_validator_interface",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:custom_tag_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/grpc/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry-proto//:logs_proto_cc",
        "@opentelemetry-proto//:logs_service_proto_cc",
    ],
)

envoy_cc_library(
    name = "http_access_log_lib",
    srcs = ["http_access_log_impl.cc"],
    hdrs = ["http_access_log_impl.h"],
    deps = [
        ":otlp_log_utils_lib",
        ":substitution_formatter_lib",
        "//envoy/access_log:access_log_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/local_info:local_info_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/thread_local:thread_local_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/config:utility_lib",
        "//source/common/http:async_client_lib",
        "//source/common/http:async_client_utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/tracing:custom_tag_lib",
        "//source/extensions/access_loggers/common:access_log_base",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/accesslog/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry-proto//:logs_proto_cc",
        "@opentelemetry-proto//:logs_service_proto_cc",
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
        "//source/extensions/access_loggers/open_telemetry:http_access_log_lib",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "@envoy_api//envoy/extensions/access_loggers/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "substitution_formatter_lib",
    srcs = ["substitution_formatter.cc"],
    hdrs = ["substitution_formatter.h"],
    deps = [
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:assert_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "@abseil-cpp//absl/strings:str_format",
        "@opentelemetry-proto//:common_proto_cc",
    ],
)

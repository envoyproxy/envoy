load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "stat_match_action_lib",
    srcs = ["stat_match_action.cc"],
    hdrs = ["stat_match_action.h"],
    deps = [
        "//envoy/registry",
        "//envoy/stats:stats_interface",
        "//source/common/matcher:matcher_lib",
        "//source/common/matcher:validation_visitor_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "open_telemetry_lib",
    srcs = ["open_telemetry_impl.cc"],
    hdrs = ["open_telemetry_impl.h"],
    deps = [
        ":stat_match_action_lib",
        "//envoy/grpc:async_client_interface",
        "//envoy/singleton:instance_interface",
        "//source/common/common:matchers_lib",
        "//source/common/grpc:async_client_lib",
        "//source/common/stats:stat_match_input_lib",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry-proto//:metrics_proto_cc",
        "@opentelemetry-proto//:metrics_service_proto_cc",
    ],
)

envoy_cc_library(
    name = "open_telemetry_http_lib",
    srcs = ["open_telemetry_http_impl.cc"],
    hdrs = ["open_telemetry_http_impl.h"],
    deps = [
        ":open_telemetry_lib",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/http:async_client_lib",
        "//source/common/http:async_client_utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/extensions/access_loggers/open_telemetry:otlp_log_utils_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "open_telemetry_proto_descriptors_lib",
    srcs = ["open_telemetry_proto_descriptors.cc"],
    hdrs = ["open_telemetry_proto_descriptors.h"],
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
        ":open_telemetry_http_lib",
        ":open_telemetry_lib",
        ":open_telemetry_proto_descriptors_lib",
        "//envoy/registry",
        "//source/server:configuration_lib",
    ],
)

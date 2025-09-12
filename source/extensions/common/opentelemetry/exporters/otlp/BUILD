load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "trace_exporter_lib",
    srcs = [
        "grpc_trace_exporter.cc",
        "http_trace_exporter.cc",
    ],
    hdrs = [
        "grpc_trace_exporter.h",
        "http_trace_exporter.h",
        "trace_exporter.h",
    ],
    deps = [
        ":otlp_utils_lib",
        ":user_agent_lib",
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:async_client_utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/tracing:trace_context_lib",
        "//source/extensions/common/opentelemetry/sdk/trace:sdk_trace_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@io_opentelemetry_cpp//api",
        "@opentelemetry_proto//:trace_proto_cc",
        "@opentelemetry_proto//:trace_service_proto_cc",
    ],
)

envoy_cc_library(
    name = "otlp_utils_lib",
    srcs = ["otlp_utils.cc"],
    hdrs = ["otlp_utils.h"],
    deps = [
        "//source/common/common:fmt_lib",
        "//source/common/common:macros",
        "//source/extensions/common/opentelemetry/sdk/common:types_lib",
        "@opentelemetry_proto//:common_proto_cc",
    ],
)

envoy_cc_library(
    name = "user_agent_lib",
    srcs = ["user_agent.cc"],
    hdrs = ["user_agent.h"],
    deps = [
        "//source/common/common:macros",
        "//source/common/version:version_lib",
    ],
)

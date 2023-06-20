load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for OpenTelemetry: https://opentelemetry.io/

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":opentelemetry_tracer_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "opentelemetry_tracer_lib",
    srcs = [
        "opentelemetry_tracer_impl.cc",
        "span_context_extractor.cc",
        "tracer.cc",
    ],
    hdrs = [
        "opentelemetry_tracer_impl.h",
        "span_context.h",
        "span_context_extractor.h",
        "tracer.h",
    ],
    deps = [
        ":grpc_trace_exporter",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@opentelemetry_proto//:trace_cc_proto",
    ],
)

envoy_cc_library(
    name = "grpc_trace_exporter",
    srcs = ["grpc_trace_exporter.cc"],
    hdrs = ["grpc_trace_exporter.h"],
    deps = [
        "//envoy/grpc:async_client_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/protobuf",
        "@opentelemetry_proto//:trace_cc_proto",
    ],
)

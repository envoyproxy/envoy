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
    copts = [
        # Make sure that headers included from opentelemetry-api use Abseil from Envoy
        # https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.14.0/api/BUILD#L32
        "-DHAVE_ABSEIL",
    ],
    deps = [
        ":trace_exporter",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "//source/extensions/tracers/opentelemetry/samplers:sampler_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@io_opentelemetry_cpp//api",
        "@opentelemetry_proto//:trace_cc_proto",
    ],
)

envoy_cc_library(
    name = "trace_exporter",
    srcs = [
        "grpc_trace_exporter.cc",
        "http_trace_exporter.cc",
        "otlp_utils.cc",
    ],
    hdrs = [
        "grpc_trace_exporter.h",
        "http_trace_exporter.h",
        "otlp_utils.h",
        "trace_exporter.h",
    ],
    deps = [
        "//envoy/grpc:async_client_manager_interface",
        "//envoy/upstream:cluster_manager_interface",
        "//source/common/grpc:typed_async_client_lib",
        "//source/common/http:async_client_utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/protobuf",
        "//source/common/tracing:trace_context_lib",
        "//source/common/version:version_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@io_opentelemetry_cpp//api",
        "@opentelemetry_proto//:trace_cc_proto",
    ],
)

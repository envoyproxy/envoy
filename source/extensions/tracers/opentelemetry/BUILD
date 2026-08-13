load(
    "//third_party/envoy/src/bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])

# Trace driver for OpenTelemetry: https://opentelemetry.io/

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    compatible_with = ["//buildenv/target:non_prod"],
    visibility = ["//third_party/envoy:envoy_extensions_visible"],
    deps = [
        ":opentelemetry_tracer_lib",
        "//third_party/envoy/src/api/envoy/config/trace/v3:pkg_cc_proto",
        "//third_party/envoy/src/source/extensions/tracers/common:factory_base_lib",
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
    compatible_with = ["//buildenv/target:non_prod"],
    copts = [
        # Make sure that headers included from opentelemetry-api use Abseil from Envoy
        # https://github.com/open-telemetry/opentelemetry-cpp/blob/v1.14.0/api/BUILD#L32
        "-DHAVE_ABSEIL",
    ],
    deps = [
        ":trace_exporter",
        "//third_party/envoy/src/api/envoy/config/trace/v3:pkg_cc_proto",
        "//third_party/envoy/src/envoy/thread_local:thread_local_interface",
        "//third_party/envoy/src/source/common/config:utility_lib",
        "//third_party/envoy/src/source/common/http:http_service_headers_lib",
        "//third_party/envoy/src/source/common/tracing:http_tracer_lib",
        "//third_party/envoy/src/source/common/tracing:tracer_config_lib",
        "//third_party/envoy/src/source/extensions/tracers/common:factory_base_lib",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "//third_party/envoy/src/source/extensions/tracers/opentelemetry/samplers:sampler_lib",
        "//third_party/fmtlib",
        "//third_party/opentelemetry/cpp/api",
        "//third_party/opentelemetry/proto/collector/trace/v1:trace_service_cc_proto",
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
    compatible_with = ["//buildenv/target:non_prod"],
    deps = [
        "//third_party/envoy/src/api/envoy/config/core/v3:pkg_cc_proto",
        "//third_party/envoy/src/envoy/config:typed_config_interface",
        "//third_party/envoy/src/envoy/grpc:async_client_manager_interface",
        "//third_party/envoy/src/envoy/server:factory_context_interface",
        "//third_party/envoy/src/envoy/server:tracer_config_interface",
        "//third_party/envoy/src/envoy/upstream:cluster_manager_interface",
        "//third_party/envoy/src/source/common/grpc:typed_async_client_lib",
        "//third_party/envoy/src/source/common/http:async_client_utility_lib",
        "//third_party/envoy/src/source/common/http:header_map_lib",
        "//third_party/envoy/src/source/common/http:http_service_headers_lib",
        "//third_party/envoy/src/source/common/http:message_lib",
        "//third_party/envoy/src/source/common/http:utility_lib",
        "//third_party/envoy/src/source/common/protobuf",
        "//third_party/envoy/src/source/common/tracing:trace_context_lib",
        "//third_party/envoy/src/source/common/tracing:tracer_config_lib",
        "//third_party/envoy/src/source/common/version:version_lib",
        "//third_party/envoy/src/source/server:generic_factory_context_lib",
        "//third_party/opentelemetry/cpp/api",
        "//third_party/opentelemetry/proto/collector/trace/v1:trace_service_cc_proto",
    ],
)

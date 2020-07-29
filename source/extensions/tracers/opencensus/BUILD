load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
    "envoy_select_google_grpc",
)

licenses(["notice"])  # Apache 2

# Trace driver for OpenCensus: https://opencensus.io/

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        ":opencensus_tracer_impl",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "opencensus_tracer_impl",
    srcs = ["opencensus_tracer_impl.cc"],
    hdrs = ["opencensus_tracer_impl.h"],
    copts = ["-Wno-unused-parameter"],
    external_deps = [
        "opencensus_trace",
        "opencensus_trace_b3",
        "opencensus_trace_cloud_trace_context",
        "opencensus_trace_grpc_trace_bin",
        "opencensus_trace_trace_context",
        "opencensus_exporter_ocagent",
        "opencensus_exporter_stdout",
        "opencensus_exporter_stackdriver",
        "opencensus_exporter_zipkin",
    ],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ] + envoy_select_google_grpc(["//source/common/grpc:google_async_client_lib"]),
)

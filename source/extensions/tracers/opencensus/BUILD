load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_select_google_grpc",
)

licenses(["notice"])  # Apache 2

# Trace driver for OpenCensus: https://opencensus.io/

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
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
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@io_opencensus_cpp//opencensus/exporters/trace/ocagent:ocagent_exporter",
        "@io_opencensus_cpp//opencensus/exporters/trace/stackdriver:stackdriver_exporter",
        "@io_opencensus_cpp//opencensus/exporters/trace/stdout:stdout_exporter",
        "@io_opencensus_cpp//opencensus/exporters/trace/zipkin:zipkin_exporter",
        "@io_opencensus_cpp//opencensus/trace",
        "@io_opencensus_cpp//opencensus/trace:b3",
        "@io_opencensus_cpp//opencensus/trace:cloud_trace_context",
        "@io_opencensus_cpp//opencensus/trace:grpc_trace_bin",
        "@io_opencensus_cpp//opencensus/trace:trace_context",
    ] + envoy_select_google_grpc(["//source/common/grpc:google_async_client_lib"]),
)

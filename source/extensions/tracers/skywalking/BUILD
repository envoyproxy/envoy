load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Trace driver for Apache SkyWalking.

envoy_extension_package()

envoy_cc_library(
    name = "trace_segment_reporter_lib",
    srcs = ["trace_segment_reporter.cc"],
    hdrs = ["trace_segment_reporter.h"],
    external_deps = [
        "cpp2sky",
    ],
    deps = [
        ":skywalking_stats_lib",
        "//include/envoy/grpc:async_client_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "skywalking_tracer_lib",
    srcs = [
        "skywalking_tracer_impl.cc",
        "tracer.cc",
    ],
    hdrs = [
        "skywalking_tracer_impl.h",
        "tracer.h",
    ],
    external_deps = [
        "cpp2sky",
    ],
    deps = [
        ":trace_segment_reporter_lib",
        "//include/envoy/common:time_interface",
        "//include/envoy/server:tracer_config_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/tracing:http_tracer_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "skywalking_stats_lib",
    hdrs = [
        "skywalking_stats.h",
    ],
    deps = [
        "//include/envoy/stats:stats_macros",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    status = "wip",
    deps = [
        ":skywalking_tracer_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

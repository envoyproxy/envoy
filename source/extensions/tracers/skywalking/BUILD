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
    external_deps = [
      "cpp2sky",
    ],
    hdrs = ["trace_segment_reporter.h"],
    deps = [
        ":skywalking_client_config_lib",
        ":skywalking_stats_lib",
        ":skywalking_types_lib",
        "//include/envoy/grpc:async_client_manager_interface",
        "//source/common/common:backoff_lib",
        "//source/common/grpc:async_client_lib",
        "@com_github_apache_skywalking_data_collect_protocol//:protocol_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "skywalking_types_lib",
    srcs = ["skywalking_types.cc"],
    hdrs = ["skywalking_types.h"],
    deps = [
        ":skywalking_stats_lib",
        "//include/envoy/common:random_generator_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/http:header_map_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "@com_github_apache_skywalking_data_collect_protocol//:protocol_cc_proto",
    ],
)

envoy_cc_library(
    name = "skywalking_client_config_lib",
    srcs = ["skywalking_client_config.cc"],
    hdrs = ["skywalking_client_config.h"],
    deps = [
        "//include/envoy/secret:secret_provider_interface",
        "//include/envoy/server:factory_context_interface",
        "//include/envoy/server:tracer_config_interface",
        "//source/common/config:datasource_lib",
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
    deps = [
        ":skywalking_client_config_lib",
        ":skywalking_types_lib",
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

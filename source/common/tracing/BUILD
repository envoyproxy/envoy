load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "null_span_lib",
    hdrs = [
        "null_span_impl.h",
    ],
    deps = [
        "//envoy/tracing:trace_driver_interface",
        "//source/common/common:empty_string",
    ],
)

envoy_cc_library(
    name = "common_values_lib",
    hdrs = [
        "common_values.h",
    ],
    deps = [
        "//source/common/singleton:const_singleton",
    ],
)

envoy_cc_library(
    name = "http_tracer_lib",
    srcs = [
        "http_tracer_impl.cc",
    ],
    hdrs = [
        "http_tracer_impl.h",
    ],
    deps = [
        ":trace_context_lib",
        ":tracer_lib",
        "//envoy/tracing:tracer_interface",
        "//source/common/formatter:substitution_format_utility_lib",
        "//source/common/formatter:substitution_formatter_lib",
        "//source/common/grpc:common_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/metadata/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tracer_lib",
    srcs = [
        "tracer_impl.cc",
    ],
    hdrs = [
        "tracer_impl.h",
    ],
    deps = [
        ":common_values_lib",
        ":null_span_lib",
        "//envoy/local_info:local_info_interface",
        "//envoy/tracing:tracer_interface",
        "//envoy/upstream:upstream_interface",
        "//source/common/stream_info:utility_lib",
    ],
)

envoy_cc_library(
    name = "tracer_config_lib",
    hdrs = [
        "tracer_config_impl.h",
    ],
    deps = [
        ":custom_tag_lib",
        "//envoy/server:tracer_config_interface",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "tracer_manager_lib",
    srcs = [
        "tracer_manager_impl.cc",
    ],
    hdrs = [
        "tracer_manager_impl.h",
    ],
    deps = [
        ":tracer_config_lib",
        "//envoy/server:tracer_config_interface",
        "//envoy/singleton:instance_interface",
        "//envoy/tracing:tracer_manager_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/tracing:tracer_lib",
    ],
)

envoy_cc_library(
    name = "trace_context_lib",
    srcs = [
        "trace_context_impl.cc",
    ],
    hdrs = [
        "trace_context_impl.h",
    ],
    deps = [
        "//envoy/http:header_map_interface",
        "//envoy/tracing:trace_context_interface",
        "//source/common/http:header_map_lib",
    ],
)

envoy_cc_library(
    name = "custom_tag_lib",
    srcs = [
        "custom_tag_impl.cc",
    ],
    hdrs = [
        "custom_tag_impl.h",
    ],
    deps = [
        ":trace_context_lib",
        "//envoy/router:router_interface",
        "//envoy/tracing:custom_tag_interface",
        "//source/common/config:metadata_lib",
        "@envoy_api//envoy/type/tracing/v3:pkg_cc_proto",
    ],
)

licenses(["notice"])  # Apache 2

# Trace driver for Zipkin (https://zipkin.io/).

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "zipkin_lib",
    srcs = [
        "span_buffer.cc",
        "span_context.cc",
        "span_context_extractor.cc",
        "tracer.cc",
        "util.cc",
        "zipkin_core_types.cc",
        "zipkin_tracer_impl.cc",
    ],
    hdrs = [
        "span_buffer.h",
        "span_context.h",
        "span_context_extractor.h",
        "tracer.h",
        "tracer_interface.h",
        "util.h",
        "zipkin_core_constants.h",
        "zipkin_core_types.h",
        "zipkin_json_field_names.h",
        "zipkin_tracer_impl.h",
    ],
    external_deps = [
        "rapidjson",
        "abseil_optional",
    ],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/local_info:local_info_interface",
        "//include/envoy/network:address_interface",
        "//include/envoy/runtime:runtime_interface",
        "//include/envoy/thread_local:thread_local_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//include/envoy/upstream:cluster_manager_interface",
        "//source/common/common:enum_to_int",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:message_lib",
        "//source/common/http:utility_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/network:address_lib",
        "//source/common/singleton:const_singleton",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_library(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//source/extensions/tracers:well_known_names",
        "//source/extensions/tracers/zipkin:zipkin_lib",
        "//source/server:configuration_lib",
    ],
)

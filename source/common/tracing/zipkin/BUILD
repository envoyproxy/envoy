package(default_visibility = ["//visibility:public"])

load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

envoy_cc_library(
    name = "zipkin_lib",
    srcs = [
        "span_buffer.cc",
        "span_context.cc",
        "tracer.cc",
        "util.cc",
        "zipkin_core_constants.cc",
        "zipkin_core_types.cc",
        "zipkin_json_field_names.cc",
    ],
    hdrs = [
        "span_buffer.h",
        "span_context.h",
        "tracer.h",
        "tracer_interface.h",
        "util.h",
        "zipkin_core_constants.h",
        "zipkin_core_types.h",
        "zipkin_json_field_names.h",
    ],
    external_deps = ["rapidjson"],
    deps = [
        "//include/envoy/common:time_interface",
        "//include/envoy/runtime:runtime_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
    ],
)

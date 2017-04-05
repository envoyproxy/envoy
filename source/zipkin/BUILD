package(default_visibility = ["//visibility:public"])

    load("//bazel:envoy_build_system.bzl", "envoy_cc_library")

        envoy_cc_library(name = "zipkin_lib", srcs = [
          "zipkin_core_constants.cc",
          "zipkin_core_types.cc",
          "zipkin_json_field_names.cc",
          "util.cc",
          "tracer.cc",
          "span_context.cc",
          "span_buffer.cc"
        ],
                         hdrs = [
                           "zipkin_core_constants.h",
                           "zipkin_core_types.h",
                           "zipkin_json_field_names.h",
                           "util.h",
                           "tracer_interface.h",
                           "tracer.h",
                           "span_context.h",
                           "span_buffer.h"
                         ],
                         external_deps = ["rapidjson"], deps = [], )

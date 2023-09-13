load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "sampler_lib",
    srcs = [
        
    ],
    hdrs = [
        "sampler.h",
    ],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/server:tracer_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
        "@opentelemetry_proto//:trace_cc_proto",
    ],
)
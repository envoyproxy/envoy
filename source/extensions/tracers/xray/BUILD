load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

# Trace driver for AWS X-Ray.

envoy_extension_package()

envoy_proto_library(
    name = "daemon",
    srcs = ["daemon.proto"],
)

envoy_cc_library(
    name = "xray_lib",
    srcs = [
        "daemon_broker.cc",
        "localized_sampling.cc",
        "tracer.cc",
        "util.cc",
        "xray_tracer_impl.cc",
    ],
    hdrs = [
        "daemon_broker.h",
        "localized_sampling.h",
        "reservoir.h",
        "sampling_strategy.h",
        "tracer.h",
        "util.h",
        "xray_configuration.h",
        "xray_tracer_impl.h",
    ],
    deps = [
        ":daemon_cc_proto",
        "//envoy/common:time_interface",
        "//envoy/server:tracer_config_interface",
        "//envoy/tracing:trace_driver_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
        "//source/common/common:random_generator_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/tracing:common_values_lib",
        "//source/common/tracing:http_tracer_lib",
        "//source/common/tracing:null_span_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":xray_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

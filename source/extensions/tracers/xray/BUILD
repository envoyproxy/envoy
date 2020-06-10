load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

# Trace driver for AWS X-Ray.

envoy_package()

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
    external_deps = [],
    deps = [
        ":daemon_cc_proto",
        "//include/envoy/common:time_interface",
        "//include/envoy/server:tracer_config_interface",
        "//include/envoy/tracing:http_tracer_interface",
        "//source/common/common:hex_lib",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/common/tracing:http_tracer_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    status = "wip",
    deps = [
        ":xray_lib",
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers:well_known_names",
        "//source/extensions/tracers/common:factory_base_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

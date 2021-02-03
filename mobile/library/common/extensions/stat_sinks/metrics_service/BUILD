load(
    "@envoy//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_proto_library(
    name = "service",
    srcs = ["service.proto"],
    has_services = True,
    deps = [
        "@envoy_api//envoy/config/core/v3:pkg",
        "@prometheus_metrics_model//:client_model",
    ],
)

envoy_proto_library(
    name = "config_proto",
    srcs = ["config.proto"],
    has_services = True,
    deps = [
        "@envoy_api//envoy/config/core/v3:pkg",
    ],
)

envoy_cc_extension(
    name = "mobile_grpc_streamer",
    srcs = ["mobile_grpc_streamer.cc"],
    hdrs = ["mobile_grpc_streamer.h"],
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":service_cc_proto",
        "@envoy//source/common/common:minimal_logger_lib",
        "@envoy//source/extensions/stat_sinks/metrics_service:metrics_service_grpc_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        ":config_proto_cc_proto",
        ":mobile_grpc_streamer",
        "@envoy//source/common/grpc:async_client_lib",
        "@envoy//source/extensions/stat_sinks/metrics_service:metrics_service_grpc_lib",
        "@envoy//source/server:configuration_lib",
    ],
)

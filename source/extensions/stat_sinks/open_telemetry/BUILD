load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "open_telemetry_lib",
    srcs = ["open_telemetry_impl.cc"],
    hdrs = ["open_telemetry_impl.h"],
    deps = [
        "//envoy/grpc:async_client_interface",
        "//envoy/singleton:instance_interface",
        "//source/common/grpc:async_client_lib",
        "@envoy_api//envoy/extensions/stat_sinks/open_telemetry/v3:pkg_cc_proto",
        "@opentelemetry_proto//:metrics_cc_proto",
    ],
)

envoy_cc_library(
    name = "open_telemetry_proto_descriptors_lib",
    srcs = ["open_telemetry_proto_descriptors.cc"],
    hdrs = ["open_telemetry_proto_descriptors.h"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/protobuf",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":open_telemetry_lib",
        ":open_telemetry_proto_descriptors_lib",
        "//envoy/registry",
        "//source/server:configuration_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [":per_route_resource_detector"],
)

envoy_cc_library(
    name = "per_route_resource_detector",
    hdrs = ["per_route_resource_detector.h"],
    deps = [
        ":resource_typed_metadata",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
    ],
)

envoy_cc_library(
    name = "resource_typed_metadata",
    srcs = ["resource_typed_metadata.cc"],
    hdrs = ["resource_typed_metadata.h"],
    deps = [
        "//envoy/router:router_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
    ],
)

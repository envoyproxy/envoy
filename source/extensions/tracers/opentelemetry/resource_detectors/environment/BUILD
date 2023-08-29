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
    deps = [
        ":resource_detector_environment_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
    ],
)

envoy_cc_library(
    name = "resource_detector_environment_lib",
    srcs = ["environment_resource_detector.cc"],
    hdrs = ["environment_resource_detector.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
    ],
)

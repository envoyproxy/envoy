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
        ":environment_resource_detector_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/resource_detectors/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "environment_resource_detector_lib",
    srcs = ["environment_resource_detector.cc"],
    hdrs = ["environment_resource_detector.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/extensions/tracers/opentelemetry/resource_detectors:resource_detector_lib",
        "@envoy_api//envoy/extensions/tracers/opentelemetry/resource_detectors/v3:pkg_cc_proto",
    ],
)

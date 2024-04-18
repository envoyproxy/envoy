load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "resource_detector_lib",
    srcs = [
        "resource_provider.cc",
    ],
    hdrs = [
        "resource_detector.h",
        "resource_provider.h",
    ],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/server:tracer_config_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

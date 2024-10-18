load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "consecutive_errors_monitor_lib",
    srcs = ["consecutive_errors_monitor.cc"],
    hdrs = ["consecutive_errors_monitor.h"],
    extra_visibility = [
        "//source/common/upstream:__subpackages__",
        "//test/common/upstream:__subpackages__",
    ],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib_header",
        "//source/extensions/outlier_detection_monitors/common:outlier_detection_monitors_lib",
        "@envoy_api//envoy/extensions/outlier_detection_monitors/consecutive_errors/v3:pkg_cc_proto",
    ],
)

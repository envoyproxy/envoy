load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
)

licenses(["notice"])  # Apache 2

envoy_cc_library(
    name = "outlier_detection_monitors_lib",
    srcs = ["monitor_base_impl.cc"],
    hdrs = ["monitor_base_impl.h"],
    visibility = [
        "//source/common/upstream:__subpackages__",
        "//source/extensions/outlier_detection_monitors:__subpackages__",
        "//test/common/upstream:__subpackages__",
    ],
    deps = [
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib_header",
        "@envoy_api//envoy/extensions/outlier_detection_monitors/common/v3:pkg_cc_proto",
    ],
)

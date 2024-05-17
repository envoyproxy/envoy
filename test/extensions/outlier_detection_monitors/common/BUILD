load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "monitor_base_test",
    srcs = ["monitor_base_test.cc"],
    extension_names = ["envoy.outlier_detection_monitors.common"],
    deps = [
        "//source/extensions/outlier_detection_monitors/common:outlier_detection_monitors_lib",
    ],
)

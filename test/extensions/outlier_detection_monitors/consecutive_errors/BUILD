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
    name = "consecutive_errors_monitor_test",
    srcs = ["consecutive_errors_monitor_test.cc"],
    extension_names = ["envoy.outlier_detection_monitors.consecutive_errors"],
    deps = [
        "//source/extensions/outlier_detection_monitors/consecutive_errors:consecutive_errors_monitor_lib",
    ],
)

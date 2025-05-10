load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "monitor_base_test",
    srcs = ["monitor_base_test.cc"],
    deps = [
        "//source/extensions/outlier_detection_monitors/common:outlier_detection_monitors_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "xray_test",
    srcs = [
        "util_test.cc",
    ],
    extension_name = "envoy.tracers.xray",
    deps = [
        "//source/extensions/tracers/xray:xray_lib",
    ],
)

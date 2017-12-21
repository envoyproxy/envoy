licenses(["notice"])  # Apache 2

load(
    "//third_party/envoy/src/bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//third_party/envoy/src/source/common/html:utility_lib",
    ],
)

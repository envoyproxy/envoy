licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "compressor_test",
    srcs = ["compressor_test.cc"],
    deps = [
        "//source/common/compressor:compressor_lib",
        "//source/common/common:assert_lib",
    ],
)
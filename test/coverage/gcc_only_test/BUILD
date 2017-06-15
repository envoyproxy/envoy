licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "gcc_only_test",
    srcs = ["gcc_only_test.cc"],
    coverage = False,
    tags = ["manual"],
)

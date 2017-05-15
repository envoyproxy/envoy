licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_binary",
    "envoy_package",
)

envoy_package()

envoy_cc_binary(
    name = "config_load_check",
    testonly = 1,
    srcs = ["config_load_check.cc"],
    deps = ["//test/config_test:config_test_lib"],
)

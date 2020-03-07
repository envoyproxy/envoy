licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
)

envoy_package()

envoy_cc_test_binary(
    name = "wee8_compile_tool",
    deps = [":wee8_compile_lib"],
)

envoy_cc_test_library(
    name = "wee8_compile_lib",
    srcs = ["wee8_compile.cc"],
    external_deps = ["wee8"],
)

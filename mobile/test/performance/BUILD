load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_binary", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_binary(
    name = "test_binary_size",
    srcs = ["test_binary_size.cc"],
    repository = "@envoy",
    deps = ["//library/common:envoy_main_interface_lib"],
)

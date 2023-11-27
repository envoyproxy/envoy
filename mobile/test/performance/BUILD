load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_binary", "envoy_mobile_package")

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_binary(
    name = "test_binary_size",
    srcs = ["test_binary_size.cc"],
    repository = "@envoy",
    stamped = True,
    deps = ["//library/common:envoy_main_interface_lib"],
)

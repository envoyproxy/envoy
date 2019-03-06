licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "init_impl_test",
    srcs = ["init_impl_test.cc"],
    deps = [
        "//source/init:init_lib",
    ],
)

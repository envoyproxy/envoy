licenses(["notice"])  # Apache 2

load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

envoy_package()

envoy_cc_test(
    name = "envoy_mobile_main_common_test",
    srcs = ["envoy_mobile_main_common_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common:envoy_mobile_main_common_lib",
    ],
)

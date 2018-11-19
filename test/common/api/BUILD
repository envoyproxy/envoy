licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "api_impl_test",
    srcs = ["api_impl_test.cc"],
    deps = [
        "//source/common/api:api_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

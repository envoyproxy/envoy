licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "extractor_test",
    srcs = [
        "extractor_test.cc",
    ],
    deps = [
        "//source/common/jwt_authn:jwt_lib",
        "//source/exe:envoy_common_lib",
        "//test/test_common:utility_lib",
    ],
)

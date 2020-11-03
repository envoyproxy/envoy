load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    deps = [
        "//source/extensions/common/matcher:matcher_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
    ],
)

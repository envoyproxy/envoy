load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "pass_through_filter_test",
    srcs = ["pass_through_filter_test.cc"],
    deps = [
        "//source/extensions/filters/network/thrift_proxy/filters:pass_through_filter_lib",
        "//test/extensions/filters/network/thrift_proxy:mocks",
    ],
)

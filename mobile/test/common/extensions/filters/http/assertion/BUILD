load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_mobile_package",
)

licenses(["notice"])  # Apache 2

envoy_mobile_package()

envoy_cc_test(
    name = "assertion_filter_test",
    srcs = ["assertion_filter_test.cc"],
    deps = [
        "//mobile/library/common/extensions/filters/http/assertion:config",
        "//mobile/library/common/extensions/filters/http/assertion:filter_cc_proto",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

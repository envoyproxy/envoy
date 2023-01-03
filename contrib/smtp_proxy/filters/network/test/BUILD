load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "smtp_filter_tests",
    srcs = [
        "smtp_filter_test.cc",
    ],
    deps = [
        "//contrib/smtp_proxy/filters/network/source:config",
        "//test/mocks/network:network_mocks",
    ],
)



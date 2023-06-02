load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "smtp_decoder_tests",
    srcs = [
        "smtp_decoder_test.cc",
    ],
    deps = [
        "//contrib/smtp_proxy/filters/network/source:decoder",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "smtp_filter_tests",
    srcs = [
        "smtp_filter_test.cc",
    ],
    deps = [
        "//contrib/smtp_proxy/filters/network/source:filter",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//test/mocks/network:network_mocks",
    ],
)

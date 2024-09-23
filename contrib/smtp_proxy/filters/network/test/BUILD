load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
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
        "//contrib/smtp_proxy/filters/network/source:filter",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "smtp_session_tests",
    srcs = [
        "smtp_session_test.cc",
    ],
    deps = [
        ":smtp_test_utils",
        "//contrib/smtp_proxy/filters/network/source:smtp_session_lib",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "smtp_filter_tests",
    srcs = [
        "smtp_filter_test.cc",
    ],
    deps = [
        ":smtp_test_utils",
        "//contrib/smtp_proxy/filters/network/source:config",
        "//contrib/smtp_proxy/filters/network/source:filter",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

envoy_cc_test(
    name = "smtp_integration_test",
    srcs = [
        "smtp_integration_test.cc",
    ],
    data = [
        "smtp_test_config.yaml-template",
        "//test/config/integration/certs",
    ],
    deps = [
        "//contrib/smtp_proxy/filters/network/source:config",
        "//contrib/smtp_proxy/filters/network/source:filter",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/starttls:config",
        "//test/integration:integration_lib",
        "//test/test_common:registry_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/smtp_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "smtp_test_utils",
    hdrs = ["smtp_test_utils.h"],
    deps = [
        "//contrib/smtp_proxy/filters/network/source:smtp_decoder_lib",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

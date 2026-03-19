load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "protocol_helpers_test",
    srcs = ["protocol_helpers_test.cc"],
    repository = "@envoy",
    deps = [
        "//contrib/ldap_proxy/filters/network/source:filter",
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_cc_test(
    name = "ldap_decoder_test",
    srcs = ["ldap_decoder_test.cc"],
    repository = "@envoy",
    deps = [
        "//contrib/ldap_proxy/filters/network/source:filter",
        "//source/common/buffer:buffer_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_test(
    name = "ldap_filter_test",
    srcs = ["ldap_filter_test.cc"],
    repository = "@envoy",
    deps = [
        "//contrib/ldap_proxy/filters/network/source:config",
        "//contrib/ldap_proxy/filters/network/source:filter",
        "//source/common/buffer:buffer_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

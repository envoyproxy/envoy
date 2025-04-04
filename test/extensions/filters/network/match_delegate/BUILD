load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/filters/network/match_delegate:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "match_delegate_integration_test",
    srcs = ["match_delegate_integration_test.cc"],
    deps = [
        "//source/extensions/filters/network/match_delegate:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:utility_lib",
    ],
)

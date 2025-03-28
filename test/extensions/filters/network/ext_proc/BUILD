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
        "//source/extensions/filters/network/ext_proc:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "ext_proc_integration_test",
    srcs = ["ext_proc_integration_test.cc"],
    deps = [
        "//source/extensions/filters/network/ext_proc:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/config:utility_lib",
        "//test/integration:base_integration_test_lib",
        "//test/integration:fake_upstream_lib",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

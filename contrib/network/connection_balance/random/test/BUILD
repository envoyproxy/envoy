load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/network/connection_balance/random/source:config",
        "//test/mocks/server:factory_context_mocks",
            "//envoy/registry",
        "//test/test_common:registry_lib",
    ],
)

envoy_cc_test(
    name = "connection_balance_test",
    srcs = ["connection_balance_test.cc"],
    
    deps = [
        "//contrib/network/connection_balance/random/source:config",
        "//envoy/registry",
        "//test/test_common:registry_lib",
    ],
)

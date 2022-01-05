load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    deps = [
        "//contrib/hyperscan/matching/input_matchers/source:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

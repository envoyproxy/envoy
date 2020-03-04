licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.retry_policy.conditional_retry",
    deps = [
        "//source/extensions/retry_policy:well_known_names",
        "//source/extensions/retry_policy/conditional_retry:config",
        "//test/test_common:utility_lib",
    ],
)

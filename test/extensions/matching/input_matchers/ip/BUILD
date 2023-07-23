load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.matching.matchers.ip"],
    deps = [
        "//source/extensions/matching/input_matchers/ip:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    extension_names = ["envoy.matching.matchers.ip"],
    deps = [
        "//source/extensions/matching/input_matchers/ip:ip_lib",
    ],
)

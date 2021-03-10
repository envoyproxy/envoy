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
    extension_name = "envoy.matching.generic_inputs.environment",
    deps = [
        "//source/extensions/matching/generic_inputs/environment:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "input_test",
    srcs = ["input_test.cc"],
    extension_name = "envoy.matching.generic_inputs.environment",
    deps = [
        "//source/extensions/matching/generic_inputs/environment:input_lib",
    ],
)

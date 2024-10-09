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
    extension_names = ["envoy.matching.matchers.runtime_fraction"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/matching/input_matchers/runtime_fraction:config",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    extension_names = ["envoy.matching.matchers.runtime_fraction"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/matching/input_matchers/runtime_fraction:runtime_fraction_lib",
        "//test/mocks/runtime:runtime_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

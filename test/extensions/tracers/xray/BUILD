licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "xray_test",
    srcs = [
        "util_test.cc",
    ],
    extension_name = "envoy.tracers.xray",
    deps = [
        "//source/extensions/tracers/xray:xray_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.tracers.xray",
    deps = [
        "//source/extensions/tracers/xray:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "xray_fuzz_test",
    srcs = ["fuzz_test.cc"],
    corpus = "wildcard_matcher_corpus",
    deps = [
        "//source/extensions/tracers/xray:xray_lib",
    ],
)

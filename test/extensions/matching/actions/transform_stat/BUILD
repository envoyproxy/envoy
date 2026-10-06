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
    name = "transform_stat_test",
    srcs = ["transform_stat_test.cc"],
    extension_names = ["envoy.matching.actions.transform_stat"],
    deps = [
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/extensions/matching/actions/transform_stat:config",
        "//source/extensions/matching/actions/transform_stat:transform_stat_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:utility_lib",
    ],
)

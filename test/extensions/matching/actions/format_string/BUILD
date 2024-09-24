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
    engflow_pool = "2core",
    extension_names = ["envoy.matching.actions.format_string"],
    deps = [
        "//source/common/formatter:formatter_extension_lib",
        "//source/extensions/matching/actions/format_string:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

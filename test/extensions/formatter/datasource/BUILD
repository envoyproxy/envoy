load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "datasource_test",
    srcs = ["datasource_test.cc"],
    extension_names = ["envoy.formatter.datasource"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:substitution_format_string_lib",
        "//source/extensions/formatter/datasource:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "datasource_integration_test",
    size = "large",
    srcs = ["datasource_integration_test.cc"],
    rbe_pool = "6gig",
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/formatter/datasource:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/formatter/datasource/v3:pkg_cc_proto",
    ],
)

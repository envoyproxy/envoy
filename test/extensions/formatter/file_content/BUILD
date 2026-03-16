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
    name = "file_content_test",
    srcs = ["file_content_test.cc"],
    extension_names = ["envoy.formatter.file_content"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/formatter:substitution_format_string_lib",
        "//source/extensions/formatter/file_content:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "file_content_integration_test",
    size = "large",
    srcs = ["file_content_integration_test.cc"],
    rbe_pool = "6gig",
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/formatter/file_content:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/formatter/file_content/v3:pkg_cc_proto",
    ],
)

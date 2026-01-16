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
    name = "json_content_parser_impl_test",
    srcs = ["json_content_parser_impl_test.cc"],
    extension_names = ["envoy.sse_content_parsers.json"],
    deps = [
        "//source/extensions/sse_content_parsers/json:json_content_parser_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/sse_content_parsers/json/v3:pkg_cc_proto",
    ],
)

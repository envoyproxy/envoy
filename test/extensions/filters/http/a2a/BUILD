load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "a2a_json_parser_test",
    srcs = ["a2a_json_parser_test.cc"],
    deps = [
        "//source/extensions/filters/http/a2a:a2a_json_parser_lib",
        "//test/test_common:utility_lib",
    ],
)

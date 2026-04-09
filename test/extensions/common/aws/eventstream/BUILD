load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "eventstream_parser_test",
    srcs = ["eventstream_parser_test.cc"],
    deps = [
        "//source/extensions/common/aws/eventstream:eventstream_parser_lib",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_fuzz_test(
    name = "eventstream_parser_fuzz_test",
    srcs = ["eventstream_parser_fuzz_test.cc"],
    corpus = "eventstream_parser_corpus",
    deps = [
        "//source/extensions/common/aws/eventstream:eventstream_parser_lib",
    ],
)

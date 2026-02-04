load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "sse_parser_test",
    srcs = ["sse_parser_test.cc"],
    deps = [
        "//source/common/http/sse:sse_parser_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "sse_parser_fuzz_test",
    srcs = ["sse_parser_fuzz_test.cc"],
    corpus = "sse_parser_corpus",
    deps = [
        "//source/common/http/sse:sse_parser_lib",
    ],
)

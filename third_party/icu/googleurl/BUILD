load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_fuzz_test(
    name = "googleurl_fuzz_test",
    srcs = ["googleurl_fuzz_test.cc"],
    corpus = "googleurl_corpus",
    external_deps = [
        "googleurl",
    ],
)

envoy_cc_test(
    name = "googleurl_test",
    srcs = ["googleurl_test.cc"],
    external_deps = [
        "googleurl",
    ],
)

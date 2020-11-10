load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
    "envoy_sh_test",
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

envoy_sh_test(
    name = "system_icu_test",
    srcs = ["system_icu_test.sh"],
    coverage = False,
    data = ["//bazel/external/icu/googleurl:googleurl_test"],
    tags = [
        "skip_on_windows",
    ],
)

test_suite(
    name = "ci_tests",
    tests = [
        "googleurl_fuzz_test",
        "googleurl_test",
        "system_icu_test",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_fuzz_test(
    name = "original_dst_fuzz_test",
    srcs = ["original_dst_fuzz_test.cc"],
    corpus = "original_dst_corpus",
    deps = [
        "//source/extensions/filters/listener/original_dst:original_dst_lib",
        "//test/extensions/filters/listener/common/fuzz:listener_filter_fuzzer_lib",
    ],
)

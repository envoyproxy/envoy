licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
)

envoy_package()

envoy_cc_fuzz_test(
    name = "compressor_fuzz_test",
    srcs = ["compressor_fuzz_test.cc"],
    corpus = "compressor_corpus",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/decompressor:decompressor_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
    ],
)

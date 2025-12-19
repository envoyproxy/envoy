load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "compressor_fuzz_input_proto",
    srcs = ["compressor_fuzz_input.proto"],
)

envoy_cc_fuzz_test(
    name = "compressor_fuzz_test",
    srcs = ["compressor_fuzz_test.cc"],
    corpus = "compressor_corpus",
    rbe_pool = "6gig",
    deps = [
        ":compressor_fuzz_input_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//source/extensions/compression/gzip/decompressor:zlib_decompressor_impl_lib",
    ],
)

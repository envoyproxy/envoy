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
    # linux_x64_xlarge: the fuzz coverage link peaks at 12139MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "tsan": {"link": "linux_x64_small"},
        "fuzz_coverage": {"link": "linux_x64_xlarge"},
    },
    deps = [
        ":compressor_fuzz_input_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//source/extensions/compression/gzip/decompressor:zlib_decompressor_impl_lib",
        "//test/fuzz:utility_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
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

envoy_cc_test(
    name = "compressor_test",
    srcs = ["zlib_compressor_impl_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "gzip_compressor_library_test",
    srcs = ["gzip_compressor_library_test.cc"],
    extension_name = "envoy.compression.gzip.compressor",
    deps = [
        "//source/extensions/compression/gzip/compressor:config",
        "//test/test_common:utility_lib",
    ],
)

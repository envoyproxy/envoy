load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "zlib_decompressor_impl_test",
    srcs = ["zlib_decompressor_impl_test.cc"],
    extension_name = "envoy.compression.gzip.decompressor",
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//source/extensions/compression/gzip/decompressor:zlib_decompressor_impl_lib",
        "//test/test_common:utility_lib",
    ],
)

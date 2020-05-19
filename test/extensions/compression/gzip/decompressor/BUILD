licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "zlib_decompressor_impl_test",
    srcs = ["zlib_decompressor_impl_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//source/extensions/compression/gzip/decompressor:zlib_decompressor_impl_lib",
        "//test/test_common:utility_lib",
    ],
)

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
    name = "compressor_test",
    srcs = ["zlib_compressor_impl_test.cc"],
    extension_name = "envoy.compression.gzip.compressor",
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/extensions/compression/gzip/compressor:config",
        "//test/test_common:utility_lib",
    ],
)

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
    srcs = ["brotli_compressor_impl_test.cc"],
    extension_names = ["envoy.compression.brotli.compressor"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/compression/brotli/compressor:config",
        "//source/extensions/compression/brotli/decompressor:decompressor_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

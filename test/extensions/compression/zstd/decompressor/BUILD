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
    name = "decompressor_test",
    srcs = ["zstd_decompressor_impl_test.cc"],
    extension_names = ["envoy.compression.zstd.decompressor"],
    rbe_pool = "2core",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/compression/zstd/compressor:compressor_lib",
        "//source/extensions/compression/zstd/decompressor:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

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
    name = "compression_test",
    size = "large",
    srcs = ["zstd_compression_test.cc"],
    data = [
        "//test/extensions/compression/zstd/test_data:dictionary",
    ],
    extension_names = [
        "envoy.compression.zstd.compressor",
        "envoy.compression.zstd.decompressor",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/compression/zstd/compressor:config",
        "//source/extensions/compression/zstd/decompressor:config",
        "//test/mocks/config:config_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

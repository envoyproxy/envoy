load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "compressor_test",
    srcs = ["qatzstd_compressor_impl_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/qat/compression/qatzstd/compressor/source:config",
        "//source/extensions/compression/zstd/decompressor:decompressor_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

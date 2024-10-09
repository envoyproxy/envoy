load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "qatzip_compressor_impl_test",
    srcs = select({
        "//bazel:linux_x86_64": ["qatzip_compressor_impl_test.cc"],
        "//conditions:default": [],
    }),
    rbe_pool = "2core",
    deps = [
        "//contrib/qat/compression/qatzip/compressor/source:config",
        "//source/extensions/compression/gzip/decompressor:zlib_decompressor_impl_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

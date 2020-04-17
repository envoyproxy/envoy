licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "gzip_compressor_library_test",
    srcs = ["gzip_compressor_library_test.cc"],
    extension_name = "envoy.compression.gzip.compressor",
    deps = [
        "//source/extensions/compression/gzip/compressor:config",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "gzip_compressor_integration_test",
    srcs = ["gzip_filter_test.cc"],
    extension_name = "envoy.compression.gzip.compressor",
    deps = [
        "//source/common/decompressor:decompressor_lib",
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/filters/http/compressor:compressor_filter_lib",
        "//test/mocks/http:http_mocks",
    ],
)

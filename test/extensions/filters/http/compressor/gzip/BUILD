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
    extension_name = "envoy.filters.http.compressor.gzip",
    deps = [
        "//source/extensions/filters/http/compressor/gzip:config",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "gzip_filter_test",
    srcs = ["gzip_filter_test.cc"],
    extension_name = "envoy.filters.http.compressor.gzip",
    deps = [
        "//source/common/decompressor:decompressor_lib",
        "//source/extensions/filters/http/compressor:compressor_filter_lib",
        "//source/extensions/filters/http/compressor/gzip:config",
        "//test/mocks/http:http_mocks",
    ],
)

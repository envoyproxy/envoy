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
    name = "compressor_filter_test",
    srcs = [
        "compressor_filter_test.cc",
    ],
    extension_name = "envoy.filters.http.compressor",
    deps = [
        "//source/extensions/filters/http/compressor:compressor_filter_lib",
        "//test/mocks/compression/compressor:compressor_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "compressor_filter_integration_test",
    srcs = [
        "compressor_filter_integration_test.cc",
    ],
    extension_name = "envoy.filters.http.compressor",
    deps = [
        "//source/common/decompressor:decompressor_lib",
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/filters/http/compressor:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

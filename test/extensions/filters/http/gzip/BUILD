licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "gzip_filter_test",
    srcs = ["gzip_filter_test.cc"],
    deps = [
        "//source/common/compressor:compressor_lib",
        "//source/common/decompressor:decompressor_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/gzip:gzip_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "gzip_filter_integration_test",
    srcs = [
        "gzip_filter_integration_test.cc",
    ],
    deps = [
        "//source/common/decompressor:decompressor_lib",
        "//source/extensions/filters/http/gzip:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

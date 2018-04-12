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
        "//source/common/http/filter:gzip_filter_lib",
        "//source/common/protobuf:utility_lib",
        "//test/test_common:utility_lib",
    ],
)

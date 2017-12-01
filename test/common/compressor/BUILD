licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "compressor_test",
    srcs = ["zlib_compressor_impl_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/compressor:compressor_lib",
        "//test/test_common:utility_lib",
    ],
)

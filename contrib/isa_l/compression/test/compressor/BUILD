load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "igzip_compressor_test",
    srcs = ["igzip_compressor_impl_test.cc"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//contrib/isa_l/compression/source/compressor:config",
        "//test/test_common:utility_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_binary",
)

envoy_package()

envoy_extension_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    extension_name = "envoy.filters.network.memcached_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/network/memcached_proxy:codec_lib",
    ],
)


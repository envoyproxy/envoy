licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "forward_original_sni_test",
    srcs = ["forward_original_sni_test.cc"],
    extension_name = "envoy.filters.network.forward_original_sni",
    deps = [
        "//source/extensions/filters/network/forward_original_sni",
        "//source/extensions/filters/network/forward_original_sni:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    extension_names = ["envoy.generic_proxy.codecs.http1"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/extensions/filters/network/generic_proxy/codecs/http1:config",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:status_utility_lib",
    ],
)

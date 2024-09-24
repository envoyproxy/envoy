load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/generic_proxy/codecs/http1:config",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

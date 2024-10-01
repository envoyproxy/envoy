load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//contrib/generic_proxy/filters/network/source/codecs/kafka:config",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

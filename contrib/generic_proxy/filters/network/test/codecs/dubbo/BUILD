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
        "//contrib/generic_proxy/filters/network/source/codecs/dubbo:config",
        "//contrib/generic_proxy/filters/network/test/mocks:codec_mocks",
        "//test/extensions/common/dubbo:mocks_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

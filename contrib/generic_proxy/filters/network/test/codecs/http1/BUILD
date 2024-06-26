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
    deps = [
        "//contrib/generic_proxy/filters/network/source/codecs/http1:config",
        "//contrib/generic_proxy/filters/network/test/mocks:codec_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "router_test",
    srcs = [
        "router_test.cc",
    ],
    deps = [
        "//contrib/generic_proxy/filters/network/source/router:router_lib",
        "//contrib/generic_proxy/filters/network/test:fake_codec_lib",
        "//contrib/generic_proxy/filters/network/test/mocks:codec_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:filter_mocks",
        "//contrib/generic_proxy/filters/network/test/mocks:route_mocks",
        "//source/common/buffer:buffer_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    deps = [
        "//contrib/generic_proxy/filters/network/source/router:config",
        "//contrib/generic_proxy/filters/network/test/mocks:filter_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

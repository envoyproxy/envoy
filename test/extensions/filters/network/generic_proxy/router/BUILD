load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "router_test",
    srcs = [
        "router_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/generic_proxy/router:router_lib",
        "//test/extensions/filters/network/generic_proxy:fake_codec_lib",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:route_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "upstream_test",
    srcs = [
        "upstream_test.cc",
    ],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/generic_proxy/router:router_lib",
        "//test/extensions/filters/network/generic_proxy:fake_codec_lib",
        "//test/extensions/filters/network/generic_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/generic_proxy/mocks:route_mocks",
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
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/generic_proxy/router:config",
        "//test/extensions/filters/network/generic_proxy/mocks:filter_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

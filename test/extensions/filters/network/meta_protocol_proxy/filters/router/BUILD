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
    name = "router_test",
    srcs = [
        "router_test.cc",
    ],
    extension_names = ["envoy.filters.network.meta_protocol_proxy"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/meta_protocol_proxy/filters/router:router_lib",
        "//test/extensions/filters/network/meta_protocol_proxy:fake_codec_lib",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:codec_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:filter_mocks",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:route_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    extension_names = ["envoy.filters.network.meta_protocol_proxy"],
    deps = [
        "//source/extensions/filters/network/meta_protocol_proxy/filters/router:config",
        "//test/extensions/filters/network/meta_protocol_proxy/mocks:filter_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "proxy_protocol_test",
    srcs = ["proxy_protocol_test.cc"],
    extension_name = "envoy.filters.network.proxy_protocol",
    deps = [
        "//source/extensions/filters/network/proxy_protocol:proxy_protocol_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "proxy_protocol_integration_test",
    srcs = ["proxy_protocol_integration_test.cc"],
    extension_name = "envoy.filters.network.proxy_protocol",
    deps = [
        "//source/extensions/filters/network/proxy_protocol:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/extensions/filters/network/proxy_protocol/v3:pkg_cc_proto",
    ],
)

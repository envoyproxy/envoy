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
    name = "udp_proxy_filter_test",
    srcs = ["udp_proxy_filter_test.cc"],
    extension_name = "envoy.filters.udp_listener.udp_proxy",
    deps = [
        "//source/extensions/filters/udp/udp_proxy:udp_proxy_filter_lib",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/filter/udp/udp_proxy/v2alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "udp_proxy_integration_test",
    srcs = ["udp_proxy_integration_test.cc"],
    extension_name = "envoy.filters.udp_listener.udp_proxy",
    deps = [
        "//source/extensions/filters/udp/udp_proxy:config",
        "//test/integration:integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

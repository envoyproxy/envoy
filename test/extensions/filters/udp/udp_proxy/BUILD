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
    name = "udp_proxy_integration_test",
    srcs = ["udp_proxy_integration_test.cc"],
    extension_name = "envoy.filters.udp.udp_proxy",
    deps = [
        "//source/extensions/filters/udp/udp_proxy:config",
        "//test/integration:integration_lib",
    ],
)

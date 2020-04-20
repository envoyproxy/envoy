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
    name = "dns_filter_test",
    srcs = ["dns_filter_test.cc"],
    extension_name = "envoy.filters.udp_listener.dns_filter",
    deps = [
        "//source/extensions/filters/udp/dns_filter:dns_filter_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/filter/udp/dns_filter/v2alpha:pkg_cc_proto",
    ],
)

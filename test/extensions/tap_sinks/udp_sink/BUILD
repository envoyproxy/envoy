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
    name = "udp_sink_test",
    srcs = ["udp_sink_test.cc"],
    extension_names = ["envoy.filters.http.basic_auth"],
    deps = [
        "//source/extensions/tap_sinks/udp_sink:config",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/tap/v3:pkg_cc_proto",
        "@envoy_api//envoy/data/tap/v3:pkg_cc_proto",
    ],
)

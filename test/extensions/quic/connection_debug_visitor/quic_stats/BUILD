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
    name = "quic_stats_test",
    srcs = ["quic_stats_test.cc"],
    extension_names = ["envoy.quic.connection_debug_visitor.quic_stats"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic/connection_debug_visitor/quic_stats:quic_stats_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.quic.connection_debug_visitor.quic_stats"],
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic/connection_debug_visitor/quic_stats:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/quic/connection_debug_visitor/quic_stats/v3:pkg_cc_proto",
    ],
)

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
    srcs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["quic_stats_test.cc"],
    }),
    extension_names = ["envoy.quic.connection_debug_visitor.quic_stats"],
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            "//source/extensions/quic/connection_debug_visitor/quic_stats:quic_stats_lib",
            "//test/mocks/event:event_mocks",
        ],
    }),
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = select({
        "//bazel:disable_http3": [],
        "//conditions:default": ["integration_test.cc"],
    }),
    extension_names = ["envoy.quic.connection_debug_visitor.quic_stats"],
    rbe_pool = "2core",
    deps = select({
        "//bazel:disable_http3": [],
        "//conditions:default": [
            "//source/extensions/quic/connection_debug_visitor/quic_stats:config",
            "//test/integration:http_integration_lib",
            "@envoy_api//envoy/extensions/quic/connection_debug_visitor/quic_stats/v3:pkg_cc_proto",
            "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        ],
    }),
)

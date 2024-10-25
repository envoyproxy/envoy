load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "quic_stats_test",
    srcs = ["quic_stats_test.cc"],
    deps = [
        "//source/extensions/quic/connection_debug_visitor/quic_stats:quic_stats_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = ["//test/config/integration/certs"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/quic/connection_debug_visitor/quic_stats:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/quic/connection_debug_visitor/quic_stats/v3:pkg_cc_proto",
    ],
)

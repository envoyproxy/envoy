load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_select_enable_http3",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "quic_lb_test",
    srcs = envoy_select_enable_http3(["quic_lb_test.cc"]),
    extension_names = ["envoy.quic.connection_id_generator.quic_lb"],
    rbe_pool = "6gig",
    deps = envoy_select_enable_http3([
        "//source/extensions/quic/connection_id_generator/quic_lb:quic_lb_lib",
        "//test/mocks/server:factory_context_mocks",
        "@quiche//:quic_test_tools_test_utils_lib",
    ]),
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = envoy_select_enable_http3(["integration_test.cc"]),
    extension_names = ["envoy.quic.connection_id_generator.quic_lb"],
    rbe_pool = "4core",
    deps = envoy_select_enable_http3([
        "//source/extensions/quic/connection_id_generator/quic_lb:quic_lb_config",
        "//test/integration:http_integration_lib",
        "//test/integration:quic_http_integration_test_lib",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/quic_lb/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ]),
)

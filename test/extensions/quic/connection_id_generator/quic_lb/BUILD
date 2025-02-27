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
    name = "quic_lb_test",
    srcs = ["quic_lb_test.cc"],
    extension_names = ["envoy.quic.connection_id_generator.quic_lb"],
    rbe_pool = "6gig",
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic/connection_id_generator/quic_lb:quic_lb_lib",
        "//test/mocks/server:factory_context_mocks",
        "@com_github_google_quiche//:quic_test_tools_test_utils_lib",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.quic.connection_id_generator.quic_lb"],
    rbe_pool = "4core",
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic/connection_id_generator/quic_lb:quic_lb_config",
        "//test/integration:http_integration_lib",
        "//test/integration:quic_http_integration_test_lib",
        "@envoy_api//envoy/extensions/quic/connection_id_generator/quic_lb/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ],
)

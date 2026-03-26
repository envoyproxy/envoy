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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.tls.certificate_selectors.filter_state"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/transport_sockets/tls/cert_mappers/sni:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/filter_state:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3:pkg_cc_proto",
    ],
)

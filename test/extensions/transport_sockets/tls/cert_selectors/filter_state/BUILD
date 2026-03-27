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
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/transport_sockets/tls/cert_mappers/sni:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/filter_state:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = [
        "envoy.tls.certificate_selectors.filter_state",
        "envoy.tls.certificate_mappers.sni",
    ],
    deps = [
        "//source/extensions/filters/listener/set_filter_state:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls/cert_mappers/sni:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/filter_state:config",
        "//test/config/integration/certs:certs_info",
        "//test/integration:tcp_proxy_integration_test_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/common/set_filter_state/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/listener/set_filter_state/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/filter_state/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

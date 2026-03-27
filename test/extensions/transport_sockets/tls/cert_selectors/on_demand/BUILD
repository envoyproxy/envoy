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
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.tls.certificate_selectors.on_demand_secret"],
    deps = [
        "//source/common/config:utility_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/transport_sockets/tls/cert_mappers/filter_state_override:config",
        "//source/extensions/transport_sockets/tls/cert_mappers/sni:config",
        "//source/extensions/transport_sockets/tls/cert_mappers/static_name:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/on_demand:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = [
        "integration_test.cc",
    ],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = [
        "envoy.tls.certificate_selectors.on_demand_secret",
        "envoy.tls.certificate_mappers.static_name",
        "envoy.tls.certificate_mappers.sni",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/transport_sockets/tls:config",
        "//source/extensions/transport_sockets/tls/cert_mappers/filter_state_override:config",
        "//source/extensions/transport_sockets/tls/cert_mappers/sni:config",
        "//source/extensions/transport_sockets/tls/cert_mappers/static_name:config",
        "//source/extensions/transport_sockets/tls/cert_selectors/on_demand:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config/integration/certs:certs_info",
        "//test/integration:tcp_proxy_integration_test_lib",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/filter_state_override/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)

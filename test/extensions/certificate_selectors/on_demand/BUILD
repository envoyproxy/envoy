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
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/config:api_version_lib",
        "//source/common/config:protobuf_link_hacks",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/common/tls:context_config_lib",
        "//source/common/tls:context_lib",
        "//source/extensions/certificate_selectors/on_demand:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/config/integration/certs:certs_info",
        "//test/integration:http_integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:resources_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/certificate_selectors/on_demand_secret/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/secret/v3:pkg_cc_proto",
    ],
)

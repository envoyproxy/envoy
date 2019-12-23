licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "quic_http_integration_test",
    srcs = ["quic_http_integration_test.cc"],
    data = ["//test/config/integration/certs"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/quic_listeners/quiche:active_quic_listener_config_lib",
        "//source/extensions/quic_listeners/quiche:codec_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_client_connection_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_client_session_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_connection_helper_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_proof_verifier_lib",
        "//source/extensions/quic_listeners/quiche:quic_transport_socket_factory_lib",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/api/v2/auth:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v2:pkg_cc_proto",
        "@envoy_api//envoy/config/filter/network/http_connection_manager/v2:pkg_cc_proto",
    ],
)

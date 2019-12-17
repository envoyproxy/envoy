licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_select_google_grpc",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.transport_sockets.alts",
    deps = [
        "//source/extensions/transport_sockets/alts:config",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "tsi_frame_protector_test",
    srcs = ["tsi_frame_protector_test.cc"],
    extension_name = "envoy.transport_sockets.alts",
    deps = [
        "//source/extensions/transport_sockets/alts:tsi_frame_protector",
        "//test/mocks/buffer:buffer_mocks",
    ],
)

envoy_extension_cc_test(
    name = "tsi_handshaker_test",
    srcs = ["tsi_handshaker_test.cc"],
    extension_name = "envoy.transport_sockets.alts",
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/extensions/transport_sockets/alts:tsi_handshaker",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_extension_cc_test(
    name = "tsi_socket_test",
    srcs = ["tsi_socket_test.cc"],
    extension_name = "envoy.transport_sockets.alts",
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/extensions/transport_sockets/alts:tsi_socket",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "noop_transport_socket_callbacks_test",
    srcs = ["noop_transport_socket_callbacks_test.cc"],
    extension_name = "envoy.transport_sockets.alts",
    deps = [
        "//source/extensions/transport_sockets/alts:noop_transport_socket_callbacks_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "alts_integration_test",
    srcs = envoy_select_google_grpc(["alts_integration_test.cc"]),
    extension_name = "envoy.transport_sockets.alts",
    external_deps = [
        "grpc_alts_fake_handshaker_server",
    ],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/transport_sockets/alts:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/transport_socket/alts/v2alpha:pkg_cc_proto",
    ],
)

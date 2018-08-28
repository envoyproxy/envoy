licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

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
    name = "noop_transport_socket_callbacks_test",
    srcs = ["noop_transport_socket_callbacks_test.cc"],
    extension_name = "envoy.transport_sockets.alts",
    deps = [
        "//source/extensions/transport_sockets/alts:noop_transport_socket_callbacks_lib",
        "//test/mocks/network:network_mocks",
    ],
)

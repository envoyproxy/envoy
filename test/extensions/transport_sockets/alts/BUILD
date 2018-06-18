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

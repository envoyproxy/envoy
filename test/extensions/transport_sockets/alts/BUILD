load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_select_google_grpc",
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
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//source/common/singleton:manager_impl_lib",
        "//source/extensions/transport_sockets/alts:config",
        "//source/extensions/transport_sockets/alts:tsi_socket",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "alts_channel_pool_test",
    srcs = ["alts_channel_pool_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//envoy/network:address_interface",
        "//source/extensions/transport_sockets/alts:alts_channel_pool",
        "//source/extensions/transport_sockets/alts:handshaker_cc_grpc",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_grpc_grpc//:grpc++",
    ],
)

envoy_extension_cc_test(
    name = "alts_proxy_test",
    srcs = ["alts_proxy_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//envoy/network:address_interface",
        "//source/extensions/transport_sockets/alts:alts_proxy",
        "//source/extensions/transport_sockets/alts:handshaker_cc_grpc",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

envoy_extension_cc_test(
    name = "alts_tsi_handshaker_test",
    srcs = ["alts_tsi_handshaker_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//envoy/network:address_interface",
        "//source/extensions/transport_sockets/alts:alts_proxy",
        "//source/extensions/transport_sockets/alts:alts_tsi_handshaker",
        "//source/extensions/transport_sockets/alts:handshaker_cc_grpc",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_grpc_grpc//:grpc++",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

envoy_extension_cc_test(
    name = "tsi_frame_protector_test",
    srcs = ["tsi_frame_protector_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/transport_sockets/alts:tsi_frame_protector",
        "//test/mocks/buffer:buffer_mocks",
    ],
)

envoy_extension_cc_test(
    name = "tsi_handshaker_test",
    srcs = ["tsi_handshaker_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:address_interface",
        "//source/extensions/transport_sockets/alts:alts_proxy",
        "//source/extensions/transport_sockets/alts:alts_tsi_handshaker",
        "//source/extensions/transport_sockets/alts:handshaker_cc_grpc",
        "//source/extensions/transport_sockets/alts:tsi_handshaker",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/event:event_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/status",
        "@com_google_absl//absl/strings",
        "@com_google_absl//absl/synchronization",
    ],
)

envoy_extension_cc_test(
    name = "tsi_socket_test",
    srcs = ["tsi_socket_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    external_deps = ["grpc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:address_interface",
        "//source/extensions/transport_sockets/alts:handshaker_cc_grpc",
        "//source/extensions/transport_sockets/alts:tsi_socket",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "noop_transport_socket_callbacks_test",
    srcs = ["noop_transport_socket_callbacks_test.cc"],
    extension_names = ["envoy.transport_sockets.alts"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/transport_sockets/alts:noop_transport_socket_callbacks_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "alts_integration_test",
    size = "large",
    srcs = envoy_select_google_grpc(["alts_integration_test.cc"]),
    extension_names = ["envoy.transport_sockets.alts"],
    external_deps = ["grpc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:utility_lib",
        "//source/extensions/transport_sockets/alts:config",
        "//source/extensions/transport_sockets/alts:handshaker_cc_grpc",
        "//source/extensions/transport_sockets/alts:tsi_socket",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:decode_dynamic_metadata_filter_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/secret:secret_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/transport_socket/alts/v2alpha:pkg_cc_proto",
    ],
)

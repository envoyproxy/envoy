load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "envoy_quic_alarm_lib",
    srcs = ["envoy_quic_alarm.cc"],
    hdrs = ["envoy_quic_alarm.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/event:timer_interface",
        "@com_googlesource_quiche//:quic_core_alarm_interface_lib",
        "@com_googlesource_quiche//:quic_core_clock_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_alarm_factory_lib",
    srcs = ["envoy_quic_alarm_factory.cc"],
    hdrs = ["envoy_quic_alarm_factory.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_alarm_lib",
        "@com_googlesource_quiche//:quic_core_alarm_factory_interface_lib",
        "@com_googlesource_quiche//:quic_core_arena_scoped_ptr_lib",
        "@com_googlesource_quiche//:quic_core_one_block_arena_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_connection_helper_lib",
    hdrs = ["envoy_quic_connection_helper.h"],
    tags = ["nofips"],
    deps = [
        "//source/extensions/quic_listeners/quiche/platform:envoy_quic_clock_lib",
        "@com_googlesource_quiche//:quic_core_buffer_allocator_lib",
        "@com_googlesource_quiche//:quic_core_connection_lib",
        "@com_googlesource_quiche//:quic_core_crypto_random_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_packet_writer_lib",
    srcs = ["envoy_quic_packet_writer.cc"],
    hdrs = ["envoy_quic_packet_writer.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_utils_lib",
        "@com_googlesource_quiche//:quic_core_packet_writer_interface_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_fake_proof_source_lib",
    hdrs = ["envoy_quic_fake_proof_source.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "@com_googlesource_quiche//:quic_core_crypto_proof_source_interface_lib",
        "@com_googlesource_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_source_lib",
    srcs = ["envoy_quic_proof_source.cc"],
    hdrs = ["envoy_quic_proof_source.h"],
    external_deps = ["ssl"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_fake_proof_source_lib",
        ":envoy_quic_utils_lib",
        ":quic_io_handle_wrapper_lib",
        ":quic_transport_socket_factory_lib",
        "//include/envoy/ssl:tls_certificate_config_interface",
        "//source/extensions/transport_sockets:well_known_names",
        "//source/server:connection_handler_lib",
        "@com_googlesource_quiche//:quic_core_crypto_certificate_view_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_proof_verifier_lib",
    hdrs = ["envoy_quic_fake_proof_verifier.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "@com_googlesource_quiche//:quic_core_crypto_crypto_handshake_lib",
        "@com_googlesource_quiche//:quic_core_versions_lib",
    ],
)

envoy_cc_library(
    name = "spdy_server_push_utils_for_envoy_lib",
    srcs = ["spdy_server_push_utils_for_envoy.cc"],
    tags = ["nofips"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/common:assert_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_server_push_utils_header",
    ],
)

envoy_cc_library(
    name = "envoy_quic_stream_lib",
    hdrs = ["envoy_quic_stream.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_simulated_watermark_buffer_lib",
        ":quic_filter_manager_connection_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:codec_interface",
        "//source/common/http:codec_helper_lib",
    ],
)

envoy_cc_library(
    name = "codec_lib",
    srcs = ["codec_impl.cc"],
    hdrs = ["codec_impl.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_client_session_lib",
        ":envoy_quic_server_session_lib",
        "//include/envoy/http:codec_interface",
        "//include/envoy/registry",
        "//source/common/http/http3:quic_codec_factory_lib",
        "//source/common/http/http3:well_known_names",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_library(
    name = "quic_filter_manager_connection_lib",
    srcs = ["quic_filter_manager_connection_impl.cc"],
    hdrs = ["quic_filter_manager_connection_impl.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_connection_lib",
        ":envoy_quic_simulated_watermark_buffer_lib",
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/http:header_map_lib",
        "//source/common/network:connection_base_lib",
        "//source/common/stream_info:stream_info_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_server_session_lib",
    srcs = [
        "envoy_quic_server_session.cc",
        "envoy_quic_server_stream.cc",
    ],
    hdrs = [
        "envoy_quic_server_session.h",
        "envoy_quic_server_stream.h",
    ],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_crypto_server_stream_lib",
        ":envoy_quic_stream_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/quic_listeners/quiche/platform:quic_platform_mem_slice_storage_impl_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_client_session_lib",
    srcs = [
        "envoy_quic_client_session.cc",
        "envoy_quic_client_stream.cc",
    ],
    hdrs = [
        "envoy_quic_client_session.h",
        "envoy_quic_client_stream.h",
    ],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_client_connection_lib",
        ":envoy_quic_stream_lib",
        ":envoy_quic_utils_lib",
        ":quic_filter_manager_connection_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/quic_listeners/quiche/platform:quic_platform_mem_slice_storage_impl_lib",
        "@com_googlesource_quiche//:quic_core_http_client_lib",
    ],
)

envoy_cc_library(
    name = "quic_io_handle_wrapper_lib",
    hdrs = ["quic_io_handle_wrapper.h"],
    deps = [
        "//include/envoy/network:io_handle_interface",
        "//source/common/network:io_socket_error_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_connection_lib",
    srcs = ["envoy_quic_connection.cc"],
    hdrs = ["envoy_quic_connection.h"],
    tags = ["nofips"],
    deps = [
        ":quic_io_handle_wrapper_lib",
        "//include/envoy/network:connection_interface",
        "//source/common/network:listen_socket_lib",
        "//source/extensions/quic_listeners/quiche:envoy_quic_utils_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "@com_googlesource_quiche//:quic_core_connection_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_server_connection_lib",
    srcs = ["envoy_quic_server_connection.cc"],
    hdrs = ["envoy_quic_server_connection.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_connection_lib",
        "//source/server:connection_handler_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_client_connection_lib",
    srcs = ["envoy_quic_client_connection.cc"],
    hdrs = ["envoy_quic_client_connection.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_connection_lib",
        ":envoy_quic_packet_writer_lib",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/network:socket_option_factory_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "envoy_quic_dispatcher_lib",
    srcs = ["envoy_quic_dispatcher.cc"],
    hdrs = ["envoy_quic_dispatcher.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_server_connection_lib",
        ":envoy_quic_server_session_lib",
        "//include/envoy/network:listener_interface",
        "//source/common/http:utility_lib",
        "//source/server:connection_handler_lib",
        "@com_googlesource_quiche//:quic_core_server_lib",
        "@com_googlesource_quiche//:quic_core_utils_lib",
    ],
)

envoy_cc_library(
    name = "envoy_quic_simulated_watermark_buffer_lib",
    hdrs = ["envoy_quic_simulated_watermark_buffer.h"],
    deps = ["//source/common/common:assert_lib"],
)

envoy_cc_library(
    name = "active_quic_listener_lib",
    srcs = ["active_quic_listener.cc"],
    hdrs = ["active_quic_listener.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_alarm_factory_lib",
        ":envoy_quic_connection_helper_lib",
        ":envoy_quic_dispatcher_lib",
        ":envoy_quic_packet_writer_lib",
        ":envoy_quic_proof_source_lib",
        ":envoy_quic_utils_lib",
        "//include/envoy/network:listener_interface",
        "//source/common/network:listener_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/server:connection_handler_lib",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "active_quic_listener_config_lib",
    srcs = ["active_quic_listener_config.cc"],
    hdrs = ["active_quic_listener_config.h"],
    tags = ["nofips"],
    deps = [
        ":active_quic_listener_lib",
        "//include/envoy/registry",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "envoy_quic_utils_lib",
    srcs = ["envoy_quic_utils.cc"],
    hdrs = ["envoy_quic_utils.h"],
    external_deps = ["quiche_quic_platform"],
    tags = ["nofips"],
    deps = [
        "//include/envoy/http:codec_interface",
        "//source/common/http:header_map_lib",
        "//source/common/network:address_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/network:socket_option_factory_lib",
        "@com_googlesource_quiche//:quic_core_http_header_list_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "quic_transport_socket_factory_lib",
    srcs = ["quic_transport_socket_factory.cc"],
    hdrs = ["quic_transport_socket_factory.h"],
    security_posture = "unknown",
    tags = ["nofips"],
    deps = [
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//include/envoy/ssl:context_config_interface",
        "//source/common/common:assert_lib",
        "//source/extensions/transport_sockets:well_known_names",
        "//source/extensions/transport_sockets/tls:context_config_lib",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "envoy_quic_crypto_server_stream_lib",
    srcs = ["envoy_quic_crypto_server_stream.cc"],
    hdrs = ["envoy_quic_crypto_server_stream.h"],
    tags = ["nofips"],
    deps = [
        ":envoy_quic_proof_source_lib",
        "@com_googlesource_quiche//:quic_core_http_spdy_session_lib",
    ],
)

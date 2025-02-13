load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# Built-in TLS connection transport socket.

envoy_package()

envoy_cc_library(
    name = "connection_info_impl_base_lib",
    srcs = ["connection_info_impl_base.cc"],
    hdrs = ["connection_info_impl_base.h"],
    external_deps = ["ssl"],
    visibility = ["//visibility:public"],
    deps = [
        ":context_lib",
        ":utility_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
    ],
)

envoy_cc_library(
    name = "ssl_handshaker_lib",
    srcs = ["ssl_handshaker.cc"],
    hdrs = ["ssl_handshaker.h"],
    deps = [
        ":connection_info_impl_base_lib",
        ":context_lib",
        ":utility_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/ssl:handshaker_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//envoy/ssl:ssl_socket_state",
        "//envoy/ssl/private_key:private_key_callbacks_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_annotations",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "io_handle_bio_lib",
    srcs = ["io_handle_bio.cc"],
    hdrs = ["io_handle_bio.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:io_handle_interface",
    ],
)

envoy_cc_library(
    name = "ssl_socket_base",
    srcs = ["ssl_socket.cc"],
    hdrs = ["ssl_socket.h"],
    external_deps = ["ssl"],
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":context_lib",
        ":io_handle_bio_lib",
        ":ssl_handshaker_lib",
        ":utility_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/ssl:handshaker_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//envoy/ssl:ssl_socket_state",
        "//envoy/ssl/private_key:private_key_callbacks_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_annotations",
        "//source/common/http:headers_lib",
        "//source/common/network:transport_socket_options_lib",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "client_ssl_socket_lib",
    srcs = ["client_ssl_socket.cc"],
    hdrs = ["client_ssl_socket.h"],
    external_deps = ["ssl"],
    deps = [
        ":ssl_socket_base",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "server_ssl_socket_lib",
    srcs = ["server_ssl_socket.cc"],
    hdrs = ["server_ssl_socket.h"],
    external_deps = ["ssl"],
    deps = [
        ":ssl_socket_base",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/hash",
        "@com_google_absl//absl/synchronization",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "ssl_socket_lib",
    deps = [
        ":client_ssl_socket_lib",
        ":server_ssl_socket_lib",
    ],
)

envoy_cc_library(
    name = "context_config_lib",
    srcs = ["context_config_impl.cc"],
    hdrs = ["context_config_impl.h"],
    external_deps = ["ssl"],
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":ssl_handshaker_lib",
        "//envoy/secret:secret_callbacks_interface",
        "//envoy/secret:secret_provider_interface",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/ssl:context_config_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:matchers_lib",
        "//source/common/config:datasource_lib",
        "//source/common/json:json_loader_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/secret:sds_api_lib",
        "//source/common/ssl:certificate_validation_context_config_impl_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "server_context_config_lib",
    srcs = ["server_context_config_impl.cc"],
    hdrs = ["server_context_config_impl.h"],
    deps = [
        ":context_config_lib",
        ":server_context_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "context_lib",
    srcs = [
        "client_context_impl.cc",
        "context_impl.cc",
        "context_manager_impl.cc",
    ],
    hdrs = [
        "client_context_impl.h",
        "context_impl.h",
        "context_manager_impl.h",
    ],
    external_deps = ["ssl"],
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":stats_lib",
        ":utility_lib",
        "//envoy/ssl:context_config_interface",
        "//envoy/ssl:context_interface",
        "//envoy/ssl:context_manager_interface",
        "//envoy/ssl:ssl_socket_extended_info_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:cidr_range_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/common/tls/cert_validator:cert_validator_lib",
        "//source/common/tls/private_key:private_key_manager_lib",
        "@com_github_google_quiche//:quic_core_crypto_proof_source_lib",
        "@com_google_absl//absl/container:node_hash_set",
        "@com_google_absl//absl/synchronization",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "server_context_lib",
    srcs = [
        "default_tls_certificate_selector.cc",
        "server_context_impl.cc",
    ],
    hdrs = [
        "default_tls_certificate_selector.h",
        "server_context_impl.h",
    ],
    deps = [
        ":context_lib",
        "//source/common/tls/ocsp:ocsp_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
    alwayslink = 1,  # has factory registration
)

envoy_cc_library(
    name = "stats_lib",
    srcs = ["stats.cc"],
    hdrs = ["stats.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = [
        "utility.h",
        "aws_lc_compat.h",
    ],
    external_deps = ["ssl"],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:safe_memcpy_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
    ],
)

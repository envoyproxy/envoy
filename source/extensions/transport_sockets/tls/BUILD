load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Built-in TLS connection transport socket.

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream_and_upstream",
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":ssl_socket_lib",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/registry",
        "//include/envoy/server:transport_socket_config_interface",
        "//source/extensions/transport_sockets:well_known_names",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ssl_handshaker_lib",
    srcs = ["ssl_handshaker.cc"],
    hdrs = ["ssl_handshaker.h"],
    external_deps = ["ssl"],
    deps = [
        ":context_lib",
        ":utility_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/ssl:handshaker_interface",
        "//include/envoy/ssl:ssl_socket_extended_info_interface",
        "//include/envoy/ssl:ssl_socket_state",
        "//include/envoy/ssl/private_key:private_key_callbacks_interface",
        "//include/envoy/ssl/private_key:private_key_interface",
        "//include/envoy/stats:stats_macros",
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
        "//include/envoy/buffer:buffer_interface",
        "//include/envoy/network:io_handle_interface",
    ],
)

envoy_cc_library(
    name = "ssl_socket_lib",
    srcs = ["ssl_socket.cc"],
    hdrs = ["ssl_socket.h"],
    external_deps = [
        "abseil_hash",
        "abseil_node_hash_map",
        "abseil_optional",
        "abseil_synchronization",
        "ssl",
    ],
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":context_config_lib",
        ":context_lib",
        ":io_handle_bio_lib",
        ":ssl_handshaker_lib",
        ":utility_lib",
        "//include/envoy/network:connection_interface",
        "//include/envoy/network:transport_socket_interface",
        "//include/envoy/ssl:handshaker_interface",
        "//include/envoy/ssl:ssl_socket_extended_info_interface",
        "//include/envoy/ssl:ssl_socket_state",
        "//include/envoy/ssl/private_key:private_key_callbacks_interface",
        "//include/envoy/ssl/private_key:private_key_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:thread_annotations",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_library(
    name = "context_config_lib",
    srcs = ["context_config_impl.cc"],
    hdrs = ["context_config_impl.h"],
    external_deps = [
        "ssl",
    ],
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":ssl_handshaker_lib",
        "//include/envoy/secret:secret_callbacks_interface",
        "//include/envoy/secret:secret_provider_interface",
        "//include/envoy/server:transport_socket_config_interface",
        "//include/envoy/ssl:context_config_interface",
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
    name = "context_lib",
    srcs = [
        "context_impl.cc",
        "context_manager_impl.cc",
    ],
    hdrs = [
        "context_impl.h",
        "context_manager_impl.h",
    ],
    external_deps = [
        "abseil_node_hash_set",
        "abseil_synchronization",
        "ssl",
    ],
    # TLS is core functionality.
    visibility = ["//visibility:public"],
    deps = [
        ":utility_lib",
        "//include/envoy/ssl:context_config_interface",
        "//include/envoy/ssl:context_interface",
        "//include/envoy/ssl:context_manager_interface",
        "//include/envoy/ssl:ssl_socket_extended_info_interface",
        "//include/envoy/ssl/private_key:private_key_interface",
        "//include/envoy/stats:stats_interface",
        "//include/envoy/stats:stats_macros",
        "//source/common/common:assert_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/runtime:runtime_features_lib",
        "//source/common/stats:symbol_table_lib",
        "//source/common/stats:utility_lib",
        "//source/extensions/transport_sockets/tls/ocsp:ocsp_lib",
        "//source/extensions/transport_sockets/tls/private_key:private_key_manager_lib",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "utility_lib",
    srcs = ["utility.cc"],
    hdrs = ["utility.h"],
    external_deps = [
        "ssl",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
    ],
)

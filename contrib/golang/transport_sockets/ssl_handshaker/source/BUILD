load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",

    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

# custom TLS handshaker extension.

envoy_extension_package()

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":custom_ssl_handshaker_lib",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/extensions/transport_sockets:well_known_names",
        "//source/common/config:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//contrib/envoy/extensions/transport_sockets/ssl_handshaker/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "custom_ssl_handshaker_lib",
    srcs = ["custom_ssl_handshaker.cc"],
    hdrs = ["custom_ssl_handshaker.h"],
    external_deps = [
        "abseil_synchronization",
    ],
    deps = [
        ":cgo_adapter",
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
        "//source/common/common:callback_impl_lib",
        "//source/common/http:headers_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/server:transport_socket_config_lib",
        "//source/extensions/transport_sockets/tls:ssl_handshaker_lib",
        "@envoy_api//contrib/envoy/extensions/transport_sockets/ssl_handshaker/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "cgo_adapter",
    srcs = ["cgo_adapter.cc"],
    hdrs = ["custom_ssl_handshaker.h" ],
    deps = [
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
        "//source/common/common:callback_impl_lib",
        "//source/common/http:headers_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/server:transport_socket_config_lib",
        "//source/extensions/transport_sockets/tls:ssl_handshaker_lib",
        "//source/extensions/filters/common/dso:dso_lib",
        "@envoy_api//contrib/envoy/extensions/transport_sockets/ssl_handshaker/v3alpha:pkg_cc_proto",
    ],
)


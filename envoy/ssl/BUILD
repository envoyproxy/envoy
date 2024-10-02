load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "connection_interface",
    hdrs = ["connection.h"],
    deps = [
        ":ssl_socket_state",
        "//envoy/common:time_interface",
        "@com_google_absl//absl/types:optional",
    ],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
    deps = [
        "//source/common/network:cidr_range_interface",
        "@envoy_api//envoy/admin/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "context_config_interface",
    hdrs = ["context_config.h"],
    deps = [
        ":certificate_validation_context_config_interface",
        ":handshaker_interface",
        ":tls_certificate_config_interface",
        "//source/common/network:cidr_range_interface",
    ],
)

envoy_cc_library(
    name = "context_manager_interface",
    hdrs = ["context_manager.h"],
    deps = [
        ":context_config_interface",
        ":context_interface",
        "//envoy/common:time_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "tls_certificate_config_interface",
    hdrs = ["tls_certificate_config.h"],
    deps = [
        "//envoy/ssl/private_key:private_key_interface",
    ],
)

envoy_cc_library(
    name = "certificate_validation_context_config_interface",
    hdrs = ["certificate_validation_context_config.h"],
    deps = [
        "@com_google_absl//absl/types:optional",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ssl_socket_extended_info_interface",
    hdrs = ["ssl_socket_extended_info.h"],
)

envoy_cc_library(
    name = "ssl_socket_state",
    hdrs = ["ssl_socket_state.h"],
)

envoy_cc_library(
    name = "handshaker_interface",
    hdrs = ["handshaker.h"],
    external_deps = ["ssl"],
    deps = [
        "//envoy/api:api_interface",
        "//envoy/config:typed_config_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:post_io_action_interface",
        "//envoy/protobuf:message_validator_interface",
        "//envoy/server:lifecycle_notifier_interface",
        "//envoy/server:options_interface",
        "//envoy/singleton:manager_interface",
    ],
)

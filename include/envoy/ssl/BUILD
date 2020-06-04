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
    external_deps = ["abseil_optional"],
    deps = [
        "//include/envoy/common:time_interface",
    ],
)

envoy_cc_library(
    name = "context_interface",
    hdrs = ["context.h"],
    deps = ["@envoy_api//envoy/admin/v3:pkg_cc_proto"],
)

envoy_cc_library(
    name = "context_config_interface",
    hdrs = ["context_config.h"],
    deps = [
        ":certificate_validation_context_config_interface",
        ":tls_certificate_config_interface",
    ],
)

envoy_cc_library(
    name = "context_manager_interface",
    hdrs = ["context_manager.h"],
    deps = [
        ":context_config_interface",
        ":context_interface",
        "//include/envoy/common:time_interface",
        "//include/envoy/config:typed_config_interface",
        "//include/envoy/stats:stats_interface",
    ],
)

envoy_cc_library(
    name = "tls_certificate_config_interface",
    hdrs = ["tls_certificate_config.h"],
    deps = [
        "//include/envoy/ssl/private_key:private_key_interface",
    ],
)

envoy_cc_library(
    name = "certificate_validation_context_config_interface",
    hdrs = ["certificate_validation_context_config.h"],
    deps = [
        "//source/common/common:matchers_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ssl_socket_extended_info_interface",
    hdrs = ["ssl_socket_extended_info.h"],
    deps = [
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "cryptomb_private_key_provider_lib",
    srcs = [
        "cryptomb_private_key_provider.cc",
    ],
    hdrs = [
        "cryptomb_private_key_provider.h",
        "ipp.h",
    ],
    external_deps = ["ssl"],
    repository = "@envoy",
    deps = [
        "//bazel/foreign_cc:ipp-crypto",
        "//envoy/api:api_interface",
        "//envoy/event:dispatcher_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/singleton:manager_interface",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:thread_lib",
        "@envoy_api//envoy/extensions/private_key_providers/cryptomb/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":cryptomb_private_key_provider_lib",
        "//envoy/registry",
        "//envoy/ssl/private_key:private_key_config_interface",
        "//envoy/ssl/private_key:private_key_interface",
        "//source/common/common:logger_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/private_key_providers/cryptomb/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

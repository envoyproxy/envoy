load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "private_key_interface",
    hdrs = ["private_key.h"],
    external_deps = ["ssl"],
    deps = [
        ":private_key_callbacks_interface_no_include_prefix",
        "//include/envoy/event:dispatcher_interface_no_include_prefix",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "private_key_config_interface",
    hdrs = ["private_key_config.h"],
    deps = [
        ":private_key_interface_no_include_prefix",
        "//include/envoy/config:typed_config_interface_no_include_prefix",
        "//include/envoy/registry:registry_no_include_prefix",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "private_key_callbacks_interface",
    hdrs = ["private_key_callbacks.h"],
    external_deps = ["ssl"],
)

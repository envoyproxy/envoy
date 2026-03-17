load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "secret_manager_lib",
    srcs = ["secret_manager.cc"],
    hdrs = ["secret_manager.h"],
    deps = [
        "//envoy/server:factory_context_interface",
        "//envoy/ssl:handshaker_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_lib",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":secret_manager_lib",
        "//envoy/registry",
        "//envoy/ssl:handshaker_interface",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/set_filter_state:config",
    ],
)

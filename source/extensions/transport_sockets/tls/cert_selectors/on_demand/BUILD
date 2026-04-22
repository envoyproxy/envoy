load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/ssl:handshaker_interface",
        "//envoy/thread_local:thread_local_interface",
        "//source/common/config:utility_lib",
        "//source/common/ssl:tls_certificate_config_impl_lib",
        "//source/common/tls:context_lib",
        "//source/common/tls:server_context_lib",
        "//source/extensions/filters/network/set_filter_state:config",
        "//source/server:generic_factory_context_lib",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3:pkg_cc_proto",
    ],
)

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
    external_deps = ["ssl"],
    deps = [
        "//envoy/registry",
        "//envoy/server:factory_context_interface",
        "//envoy/ssl:handshaker_interface",
        "@envoy_api//envoy/extensions/transport_sockets/tls/cert_mappers/sni/v3:pkg_cc_proto",
    ],
)

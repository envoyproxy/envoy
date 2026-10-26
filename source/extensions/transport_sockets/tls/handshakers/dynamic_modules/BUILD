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
        "//envoy/common:exception_lib",
        "//envoy/registry",
        "//envoy/ssl:handshaker_interface",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/common:thread_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/common/tls:ssl_handshaker_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "@envoy_api//envoy/extensions/transport_sockets/tls/handshakers/dynamic_modules/v3:pkg_cc_proto",
    ],
)

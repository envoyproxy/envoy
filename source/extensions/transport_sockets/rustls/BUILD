load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "rustls_lib",
    srcs = ["rustls_impl.cc"],
    hdrs = ["rustls_impl.h"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//envoy/network:io_handle_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//envoy/upstream:host_description_interface",
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "@envoy_api//envoy/extensions/transport_sockets/rustls/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    deps = [
        ":rustls_lib",
        "//source/extensions/transport_sockets/rustls/rust:rustls_ktls_static",
    ],
)

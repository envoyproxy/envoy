load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "transport_socket_config_lib",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//source/common/common:statusor_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules:transport_socket_abi_lib",
    ],
)

envoy_cc_library(
    name = "transport_socket_lib",
    srcs = [
        "abi_impl.cc",
        "transport_socket.cc",
    ],
    hdrs = ["transport_socket.h"],
    deps = [
        ":transport_socket_config_lib",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/extensions/dynamic_modules:transport_socket_abi_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["factory.cc"],
    hdrs = ["factory.h"],
    deps = [
        ":transport_socket_config_lib",
        ":transport_socket_lib",
        "//source/common/config:utility_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/common/protobuf:utility_lib",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)

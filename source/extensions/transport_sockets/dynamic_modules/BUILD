load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "transport_socket_lib",
    srcs = [
        "config.cc",
        "transport_socket.cc",
    ],
    hdrs = [
        "config.h",
        "transport_socket.h",
    ],
    deps = [
        "//envoy/network:transport_socket_interface",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/transport_sockets/common:passthrough_lib",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "abi_impl_lib",
    srcs = ["abi_impl.cc"],
    deps = [
        ":transport_socket_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
    ],
    alwayslink = 1,
)

envoy_cc_extension(
    name = "config",
    extra_visibility = [
        "//test/extensions/transport_sockets/dynamic_modules:__subpackages__",
    ],
    deps = [
        ":abi_impl_lib",
        ":transport_socket_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "io_handle_lib",
    srcs = [
        "dynamic_module_io_handle.cc",
        "socket_interface_config.cc",
    ],
    hdrs = [
        "dynamic_module_io_handle.h",
        "socket_interface_config.h",
    ],
    deps = [
        "//source/common/common:assert_lib",
        "//source/common/common:fmt_lib",
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
        "@abseil-cpp//absl/base:core_headers",
        "@abseil-cpp//absl/container:fixed_array",
        "@abseil-cpp//absl/status",
        "@abseil-cpp//absl/status:statusor",
        "@abseil-cpp//absl/strings",
    ],
)

envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":io_handle_lib",
        "//source/common/common:assert_lib",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules/abi",
    ],
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["dynamic_module_socket_interface.cc"],
    hdrs = ["dynamic_module_socket_interface.h"],
    deps = [
        ":abi_impl",
        ":io_handle_lib",
        "//envoy/registry",
        "//envoy/server:bootstrap_extension_config_interface",
        "//source/common/api:os_sys_calls_lib",
        "//source/common/common:fmt_lib",
        "//source/common/network:address_lib",
        "//source/common/network:default_socket_interface_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/network/socket_interface/dynamic_modules/v3:pkg_cc_proto",
    ],
)

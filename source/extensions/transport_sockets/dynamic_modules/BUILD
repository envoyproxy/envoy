load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

# Engine-run kernel splice pump for the dynamic-module transport socket fast path. Relays a paired
# downstream and upstream socket, splicing the upload and reading a kTLS download with recvmsg.
envoy_cc_library(
    name = "splice_pump_lib",
    srcs = ["splice_pump.cc"],
    hdrs = ["splice_pump.h"],
    deps = [
        "//envoy/common:base_includes",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/event:schedulable_cb_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:minimal_logger_lib",
        "//source/common/common:utility_lib",
        "@abseil-cpp//absl/status",
    ],
)

envoy_cc_library(
    name = "transport_socket_lib",
    srcs = ["transport_socket.cc"],
    hdrs = ["transport_socket.h"],
    deps = [
        "//envoy/api:io_error_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/upstream:host_description_interface",
        "//source/common/common:empty_string",
        "//source/common/common:logger_lib",
        "//source/common/common:statusor_lib",
        "//source/common/network:transport_socket_options_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/dynamic_modules/abi",
    ],
)

# The transport socket ABI callbacks are strong definitions that override the weak stubs in
# source/extensions/dynamic_modules/abi_impl.cc, so they live in a dedicated alwayslink library to
# ensure they are always linked into the final binary.
envoy_cc_library(
    name = "abi_impl",
    srcs = ["abi_impl.cc"],
    deps = [
        ":splice_pump_lib",
        ":transport_socket_lib",
        "//envoy/api:io_error_interface",
        "//envoy/buffer:buffer_interface",
        "//envoy/event:deferred_deletable",
        "//envoy/event:dispatcher_interface",
        "//envoy/event:file_event_interface",
        "//envoy/event:schedulable_cb_interface",
        "//envoy/network:address_interface",
        "//envoy/network:connection_interface",
        "//envoy/network:transport_socket_interface",
        "//envoy/stream_info:stream_info_interface",
        "//source/common/common:linked_object",
        "//source/extensions/dynamic_modules:abi_impl",
        "//source/extensions/dynamic_modules/abi",
    ],
    alwayslink = True,
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":abi_impl",
        ":transport_socket_lib",
        "//envoy/registry",
        "//envoy/server:transport_socket_config_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/transport_sockets/dynamic_modules/v3:pkg_cc_proto",
    ],
)

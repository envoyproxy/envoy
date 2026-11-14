load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_dyn_module_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_dyn_module_test(
    name = "abi_impl_test",
    srcs = ["abi_impl_test.cc"],
    extension_names = ["envoy.extensions.network.socket_interface.dynamic_modules"],
    deps = [
        "//source/extensions/network/socket_interface/dynamic_modules:abi_impl",
        "//source/extensions/network/socket_interface/dynamic_modules:io_handle_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_creation_only",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_config_destroy",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_config_new",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_io_read",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_ip_family_supported",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_set_socket_handle",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_socket",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_missing_socket_close",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_test",
        "//test/extensions/dynamic_modules/test_data/rust:socket_interface_test",
    ],
    extension_names = ["envoy.extensions.network.socket_interface.dynamic_modules"],
    deps = [
        "//envoy/registry",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "//source/extensions/network/socket_interface/dynamic_modules:config",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/network/socket_interface/dynamic_modules/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "socket_interface_test",
    srcs = ["socket_interface_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_creation_only",
        "//test/extensions/dynamic_modules/test_data/c:socket_interface_test",
    ],
    extension_names = ["envoy.extensions.network.socket_interface.dynamic_modules"],
    deps = [
        "//envoy/buffer:buffer_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/extensions/network/socket_interface/dynamic_modules:abi_impl",
        "//source/extensions/network/socket_interface/dynamic_modules:config",
        "//source/extensions/network/socket_interface/dynamic_modules:io_handle_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_dyn_module_test(
    name = "socket_interface_integration_test",
    size = "large",
    srcs = ["socket_interface_integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:socket_interface_test",
    ],
    extension_names = ["envoy.extensions.network.socket_interface.dynamic_modules"],
    rbe_pool = {
        "tsan": {"build": "linux_x64_small"},
        "gcc": {"build": "linux_x64_small"},
    },
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:socket_interface_lib",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/network/socket_interface/dynamic_modules:config",
        "//test/config:utility_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/integration:integration_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/socket_interface/dynamic_modules/v3:pkg_cc_proto",
    ],
)

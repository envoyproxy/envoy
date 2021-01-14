load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "user_space_io_socket_handle_impl_test",
    srcs = ["user_space_io_socket_handle_impl_test.cc"],
    extension_name = "envoy.io_socket.user_space_socket",
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/io_socket/user_space_io_socket:user_space_io_socket_handle_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_extension_cc_test(
    name = "user_space_io_socket_handle_impl_platform_test",
    srcs = ["user_space_io_socket_handle_impl_platform_test.cc"],
    extension_name = "envoy.io_socket.user_space_socket",
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/io_socket/user_space_io_socket:user_space_io_socket_handle_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_extension_cc_test(
    name = "user_space_file_event_impl_test",
    srcs = ["user_space_file_event_impl_test.cc"],
    extension_name = "envoy.io_socket.user_space_socket",
    deps = [
        "//include/envoy/event:file_event_interface",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/io_socket/user_space_io_socket:user_space_io_socket_handle_lib",
        "//source/extensions/io_socket/user_space_io_socket:peer_buffer_lib",
        "//test/mocks:common_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

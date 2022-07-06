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
    name = "file_event_impl_test",
    srcs = ["file_event_impl_test.cc"],
    extension_names = ["envoy.io_socket.user_space"],
    deps = [
        "//envoy/event:file_event_interface",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/io_socket/user_space:file_event_lib",
        "//source/extensions/io_socket/user_space:io_handle_lib",
        "//test/mocks:common_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "io_handle_impl_test",
    srcs = ["io_handle_impl_test.cc"],
    extension_names = ["envoy.io_socket.user_space"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/io_socket/user_space:io_handle_impl_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_extension_cc_test(
    name = "connection_compatbility_test",
    srcs = ["connection_compatbility_test.cc"],
    extension_names = ["envoy.io_socket.user_space"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:utility_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/network:address_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:listen_socket_lib",
        "//source/extensions/io_socket/user_space:io_handle_impl_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:network_utility_lib",
    ],
)

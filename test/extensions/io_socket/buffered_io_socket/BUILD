load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "buffered_io_socket_handle_impl_test",
    srcs = ["buffered_io_socket_handle_impl_test.cc"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/io_socket/buffered_io_socket:buffered_io_socket_handle_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_cc_test(
    name = "buffered_io_socket_handle_impl_platform_test",
    srcs = ["buffered_io_socket_handle_impl_platform_test.cc"],
    tags = ["fails_on_windows"],
    deps = [
        "//source/common/common:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/io_socket/buffered_io_socket:buffered_io_socket_handle_lib",
        "//test/mocks/event:event_mocks",
    ],
)

envoy_cc_test(
    name = "user_space_file_event_impl_test",
    srcs = ["user_space_file_event_impl_test.cc"],
    deps = [
        "//include/envoy/event:file_event_interface",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/io_socket/buffered_io_socket:buffered_io_socket_handle_lib",
        "//source/extensions/io_socket/buffered_io_socket:peer_buffer_lib",
        "//test/mocks:common_lib",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "b_factory_test",
    srcs = ["b_factory_test.cc"],
    deps = [
        "//source/extensions/io_socket/buffered_io_socket:b_factory",
        "//test/test_common:registry_lib",
    ],
)

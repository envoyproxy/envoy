load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "reverse_conn_filter_test",
    size = "medium",
    srcs = ["reverse_conn_filter_test.cc"],
    deps = [
        "//envoy/network:connection_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/common/network:address_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:socket_interface_lib",
        "//source/common/thread_local:thread_local_lib",
        "//source/extensions/filters/http/reverse_conn:reverse_conn_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection_handshake/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/reverse_connection_socket_interface/v3:pkg_cc_proto",
    ],
)

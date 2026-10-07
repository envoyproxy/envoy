load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "hbone_integration_test",
    size = "large",
    srcs = ["hbone_integration_test.cc"],
    deps = [
        "//contrib/istio/filters/network/hbone/source:config",
        "//source/extensions/bootstrap/internal_listener:config",
        "//source/extensions/retry/host/previous_hosts:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/bootstrap/internal_listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/retry/host/previous_hosts/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//contrib/istio/filters/network/hbone/source:config",
        "//contrib/istio/filters/network/hbone/source:hbone_lib",
        "//source/common/stream_info:bool_accessor_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "hbone_test",
    srcs = ["hbone_test.cc"],
    deps = [
        "//contrib/istio/filters/network/hbone/source:hbone_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/common/network:connection_lib",
        "//source/common/network:listen_socket_lib",
        "//source/common/stream_info:filter_state_lib",
        "//source/extensions/io_socket/user_space:io_handle_impl_lib",
        "//test/mocks/network:io_handle_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

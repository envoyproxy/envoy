load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "proxy_protocol_header_test",
    srcs = ["proxy_protocol_header_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//test/mocks/network:connection_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "proxy_protocol_regression_test",
    srcs = ["proxy_protocol_regression_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/event:dispatcher_includes",
        "//source/common/event:dispatcher_lib",
        "//source/common/listener_manager:connection_handler_lib",
        "//source/common/network:connection_balancer_lib",
        "//source/common/network:listener_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//source/extensions/filters/listener/proxy_protocol:proxy_protocol_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:network_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

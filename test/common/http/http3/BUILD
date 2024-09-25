load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
    "envoy_select_enable_http3",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "conn_pool_test",
    srcs = envoy_select_enable_http3(["conn_pool_test.cc"]),
    rbe_pool = "2core",
    tags = ["nofips"],
    deps = envoy_select_enable_http3([
        "//source/common/event:dispatcher_lib",
        "//source/common/http/http3:conn_pool_lib",
        "//source/common/network:utility_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//test/common/http:common_lib",
        "//test/common/quic:test_utils_lib",
        "//test/common/upstream:utility_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:transport_socket_factory_context_mocks",
        "//test/mocks/upstream:cluster_info_mocks",
        "//test/mocks/upstream:transport_socket_match_mocks",
    ]),
)

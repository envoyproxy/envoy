load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "upstream_request_test",
    srcs = ["upstream_request_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_abi_edge_cases",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_end_stream",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_headers_end_stream",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_headers_end_stream_no_body",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_no_op",
        "//test/extensions/dynamic_modules/test_data/c:upstream_bridge_stop_and_buffer",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//source/extensions/upstreams/http/dynamic_modules:config",
        "//source/extensions/upstreams/http/dynamic_modules:upstream_request_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/router:router_filter_interface",
        "//test/mocks/router:router_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/tcp:tcp_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/rust:upstream_http_tcp_bridge",
    ],
    rbe_pool = "2core",
    shard_count = 4,
    tags = [
        "cpu:3",
    ],
    deps = [
        "//source/extensions/upstreams/http/dynamic_modules:config",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:environment_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)

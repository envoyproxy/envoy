load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "golang_bridge_integration_test",
    size = "large",
    srcs = [
        "golang_bridge_integration_test.cc",
    ],
    data = [
        "//contrib/golang/upstreams/http/tcp/test/test_data/buffered:bridge.so",
        "//contrib/golang/upstreams/http/tcp/test/test_data/header_op:bridge.so",
        "//contrib/golang/upstreams/http/tcp/test/test_data/local_reply:bridge.so",
        "//contrib/golang/upstreams/http/tcp/test/test_data/panic:bridge.so",
        "//contrib/golang/upstreams/http/tcp/test/test_data/property:bridge.so",
        "//contrib/golang/upstreams/http/tcp/test/test_data/self_half_close:bridge.so",
        "//contrib/golang/upstreams/http/tcp/test/test_data/streaming:bridge.so",
        "//test/config/integration/certs",
    ],
    rbe_pool = "2core",
    shard_count = 30,
    tags = [
        "cpu:3",
    ],
    deps = [
        "//contrib/golang/upstreams/http/tcp/source:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//source/extensions/upstreams/http/tcp:config",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/integration:tcp_tunneling_integration_lib",
        "//test/integration/filters:add_header_filter_config_lib",
        "//test/integration/filters:add_header_filter_proto_cc_proto",
        "//test/integration/filters:stop_iteration_and_continue",
        "@envoy_api//contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/access_loggers/file/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/tcp/v3:pkg_cc_proto",
    ],
)

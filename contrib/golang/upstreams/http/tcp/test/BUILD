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
        "//contrib/golang/upstreams/http/tcp/test/test_data:bridges.so",
    ],
    rbe_pool = "2core",
    shard_count = 30,
    tags = [
        "cpu:3",
    ],
    deps = [
        "//contrib/golang/upstreams/http/tcp/source:config",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//contrib/envoy/extensions/upstreams/http/tcp/golang/v3alpha:pkg_cc_proto",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
    ],
)

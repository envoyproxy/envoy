load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/stream_info:bool_accessor_lib",
        "//source/common/tcp_proxy",
        "//source/extensions/upstreams/tcp/generic:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_binary",
)

envoy_package()

envoy_extension_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/filters/network/redis_proxy:proxy_filter_lib",
        "//test/extensions/filters/network/common/redis:redis_mocks",
        "//test/mocks:common_lib",
        "//test/mocks/network:network_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.redis_proxy",
    deps = [
        "//source/extensions/filters/network/redis_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

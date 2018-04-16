licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

# The TCP proxy extension gets pulled in by the HTTP connection manager for websocket handling.
# Thus, even if we don't want to explicitly allow configuration of the "tcp_proxy" filter, we
# should always test this code which is why it's registered with envoy_cc_test().
envoy_cc_test(
    name = "tcp_proxy_test",
    srcs = ["tcp_proxy_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/common/network:address_lib",
        "//source/common/stats:stats_lib",
        "//source/common/upstream:upstream_includes",
        "//source/common/upstream:upstream_lib",
        "//source/extensions/access_loggers:well_known_names",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/filters/network/tcp_proxy",
        "//test/common/upstream:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:host_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.tcp_proxy",
    deps = [
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

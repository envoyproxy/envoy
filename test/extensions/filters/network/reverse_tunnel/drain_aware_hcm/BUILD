load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "drain_aware_server_connection_test",
    srcs = ["drain_aware_server_connection_test.cc"],
    extension_names = [
        "envoy.filters.network.reverse_tunnel_drain_aware_http_connection_manager",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/network/reverse_tunnel/drain_aware_hcm:drain_aware_config",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "drain_aware_config_test",
    srcs = ["drain_aware_config_test.cc"],
    extension_names = [
        "envoy.filters.network.reverse_tunnel_drain_aware_http_connection_manager",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/router:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//source/extensions/filters/network/reverse_tunnel/drain_aware_hcm:drain_aware_config",
        "//source/extensions/request_id/uuid:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:overload_manager_mocks",
        "//test/test_common:utility_lib",
    ],
)

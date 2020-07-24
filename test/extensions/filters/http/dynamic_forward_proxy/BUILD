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
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    extension_name = "envoy.filters.http.dynamic_forward_proxy",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/clusters:well_known_names",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_impl",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "proxy_filter_integration_test",
    srcs = ["proxy_filter_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_name = "envoy.filters.http.dynamic_forward_proxy",
    tags = ["fails_on_windows"],
    deps = [
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

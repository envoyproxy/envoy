load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.dynamic_forward_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/extensions/filters/http/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "proxy_filter_test",
    srcs = ["proxy_filter_test.cc"],
    extension_names = ["envoy.filters.http.dynamic_forward_proxy"],
    rbe_pool = "2core",
    deps = [
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/common/dynamic_forward_proxy:dns_cache_impl",
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/upstream:basic_resource_limit_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "//test/mocks/upstream:transport_socket_match_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "test_resolver_proto",
    srcs = ["test_resolver.proto"],
)

envoy_cc_test_library(
    name = "test_resolver_lib",
    srcs = ["test_resolver.cc"],
    hdrs = ["test_resolver.h"],
    deps = [
        ":test_resolver_proto_cc_proto",
        "//envoy/network:dns_resolver_interface",
        "//envoy/registry",
        "//source/common/api:os_sys_calls_lib",
        "//source/extensions/network/dns_resolver/getaddrinfo:config",
    ],
    alwayslink = 1,
)

envoy_extension_cc_test(
    name = "proxy_filter_integration_test",
    size = "large",
    srcs = ["proxy_filter_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.filters.http.dynamic_forward_proxy"],
    rbe_pool = "2core",
    # TODO(envoyproxy/windows-dev): Diagnose failure shown on clang-cl build, see:
    #   https://gist.github.com/wrowe/a152cb1d12c2f751916122aed39d8517
    tags = ["fails_on_clang_cl"],
    deps = [
        ":test_resolver_lib",
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//source/extensions/key_value/file_based:config_lib",
        "//source/extensions/network/dns_resolver/getaddrinfo:config",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:stream_info_to_headers_filter_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/quic/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

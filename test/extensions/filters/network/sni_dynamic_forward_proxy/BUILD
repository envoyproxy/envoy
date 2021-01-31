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
    extension_name = "envoy.filters.network.sni_dynamic_forward_proxy",
    deps = [
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/sni_dynamic_forward_proxy:config",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:basic_resource_limit_mocks",
        "//test/mocks/upstream:cluster_manager_mocks",
        "@envoy_api//envoy/extensions/filters/network/sni_dynamic_forward_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "proxy_filter_integration_test",
    srcs = ["proxy_filter_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_name = "envoy.filters.network.sni_dynamic_forward_proxy",
    # TODO(envoyproxy/windows-dev): Diagnose failure shown only on clang-cl build, see:
    #   https://gist.github.com/wrowe/12b0b7579791c541c789c98563999a2e
    tags = ["fails_on_clang_cl"],
    deps = [
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/filters/listener/tls_inspector:config",
        "//source/extensions/filters/network/sni_dynamic_forward_proxy:config",
        "//source/extensions/filters/network/tcp_proxy:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

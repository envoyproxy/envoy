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
    name = "cluster_test",
    srcs = ["cluster_test.cc"],
    args = [
        # Force creation of c-ares DnsResolverImpl when running test on macOS.
        "--runtime-feature-disable-for-tests=envoy.restart_features.use_apple_api_for_dns_lookups",
    ],
    data = ["//test/extensions/transport_sockets/tls/test_data:certs"],
    extension_names = ["envoy.filters.http.dynamic_forward_proxy"],
    deps = [
        "//source/common/router:string_accessor_lib",
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/network/dns_resolver/cares:config",
        "//source/extensions/transport_sockets/raw_buffer:config",
        "//source/extensions/transport_sockets/tls:config",
        "//test/common/upstream:utility_lib",
        "//test/extensions/common/dynamic_forward_proxy:mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:admin_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/mocks/server:options_mocks",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/upstream:load_balancer_context_mock",
        "//test/mocks/upstream:load_balancer_mocks",
        "//test/mocks/upstream:priority_set_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dynamic_forward_proxy/v3:pkg_cc_proto",
    ],
)

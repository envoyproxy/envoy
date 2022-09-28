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
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.alternate_protocols_cache"],
    deps = [
        "//source/extensions/filters/http/alternate_protocols_cache:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:http_server_properties_cache_mocks",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/extensions/filters/http/alternate_protocols_cache/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    srcs = ["filter_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.filters.http.alternate_protocols_cache"],
    deps = [
        "//source/extensions/filters/http/alternate_protocols_cache:config",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/integration/filters:header_to_proxy_filter_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/common/key_value/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/key_value/file_based/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

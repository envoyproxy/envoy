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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.alternate_protocols_cache"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
    },
    deps = [
        "//source/extensions/filters/http/alternate_protocols_cache:config",
        "//test/mocks/event:event_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:logging_lib",
        "@envoy_api//envoy/extensions/filters/http/alternate_protocols_cache/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.alternate_protocols_cache"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {"build": "default"},
        "openssl": {"build": "default"},
        "coverage": {
            "build": "default",
            "link": "linux_x64_small",
        },
        "tsan": {"build": "default"},
    },
    deps = [
        "//source/extensions/filters/http/alternate_protocols_cache:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/http:http_server_properties_cache_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/alternate_protocols_cache/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "filter_integration_test",
    size = "large",
    srcs = ["filter_integration_test.cc"],
    data = [
        "//test/config/integration/certs",
    ],
    extension_names = ["envoy.filters.http.alternate_protocols_cache"],
    rbe_pool = {
        "default": {"test": "linux_x64_medium"},
        "gcc": {
            "build": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "openssl": {
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "coverage": {
            "link": "linux_x64_large",
            "test": "linux_x64_medium",
        },
        "tsan": {
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
    },
    shard_count = 2,
    tags = [
        "cpu:3",
    ],
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

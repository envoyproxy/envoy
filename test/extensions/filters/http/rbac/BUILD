load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/http/rbac:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "rbac_filter_test",
    srcs = ["rbac_filter_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/extensions/filters/common/rbac:engine_mocks",
        "//test/extensions/filters/http/rbac:route_config_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/matchers/upstream_ip_port/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "rbac_filter_integration_test",
    size = "large",
    srcs = ["rbac_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    shard_count = 2,
    deps = [
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//source/extensions/filters/http/rbac:config",
        "//source/extensions/key_value/file_based:config_lib",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_mock(
    name = "route_config_mocks",
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.http.rbac"],
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

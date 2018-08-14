licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.rbac",
    deps = [
        "//source/extensions/filters/http/rbac:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "rbac_filter_test",
    srcs = ["rbac_filter_test.cc"],
    extension_name = "envoy.filters.http.rbac",
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "//test/extensions/filters/common/rbac:engine_mocks",
        "//test/extensions/filters/http/rbac:route_config_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_extension_cc_test(
    name = "rbac_filter_integration_test",
    srcs = ["rbac_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.rbac",
    deps = [
        "//source/extensions/filters/http/rbac:config",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
    ],
)

envoy_extension_cc_mock(
    name = "route_config_mocks",
    hdrs = ["mocks.h"],
    extension_name = "envoy.filters.http.rbac",
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
    ],
)

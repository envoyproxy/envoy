load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
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

envoy_cc_test_library(
    name = "mock_lib",
    srcs = [
        "mock.cc",
    ],
    hdrs = [
        "mock.h",
    ],
    deps = [
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "empty_http_filter_config_lib",
    hdrs = ["empty_http_filter_config.h"],
    deps = [
        "//include/envoy/server:filter_config_interface",
    ],
)

envoy_extension_cc_test(
    name = "jwks_fetcher_test",
    srcs = [
        "jwks_fetcher_test.cc",
    ],
    extension_name = "envoy.filters.http.jwt_authn",
    deps = [
        "//source/extensions/filters/http/common:jwks_fetcher_lib",
        "//test/extensions/filters/http/common:mock_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = [
        "utility_test.cc",
    ],
    deps = [
        "//source/extensions/filters/http/common:utility_lib",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "factory_base_test_proto",
    srcs = ["factory_base_test.proto"],
)

envoy_cc_test(
    name = "factory_base_test",
    srcs = [
        "factory_base_test.cc",
    ],
    deps = [
        ":factory_base_test_proto_cc_proto",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//test/mocks/server:server_mocks",
        "//test/test_common:test_runtime_lib",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "cors_filter_test",
    srcs = ["cors_filter_test.cc"],
    extension_name = "envoy.filters.http.cors",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/cors:config",
        "//source/extensions/filters/http/cors:cors_filter_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "cors_filter_integration_test",
    srcs = ["cors_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.cors",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/cors:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

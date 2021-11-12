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
    extension_names = ["envoy.filters.http.composite"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/composite:config",
        "//source/extensions/filters/http/composite:filter_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_extension_cc_test(
    name = "composite_integration_test",
    srcs = ["composite_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.composite"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/composite:config",
        "//source/extensions/filters/http/composite:filter_lib",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:set_response_code_filter_lib",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

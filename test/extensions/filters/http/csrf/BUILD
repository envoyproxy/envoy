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
    name = "csrf_filter_test",
    srcs = ["csrf_filter_test.cc"],
    extension_names = ["envoy.filters.http.csrf"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/csrf:config",
        "//source/extensions/filters/http/csrf:csrf_filter_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "@envoy_api//envoy/extensions/filters/http/csrf/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "csrf_filter_integration_test",
    size = "large",
    srcs = ["csrf_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.csrf"],
    deps = [
        "//source/extensions/filters/http/csrf:config",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
    ],
)

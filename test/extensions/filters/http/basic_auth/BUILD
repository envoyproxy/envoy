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
    extension_names = ["envoy.filters.http.basic_auth"],
    deps = [
        "//source/extensions/filters/http/basic_auth:basic_auth_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/basic_auth/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.basic_auth"],
    deps = [
        "//source/extensions/filters/http/basic_auth:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "basic_auth_integration_test",
    size = "large",
    srcs = ["basic_auth_integration_test.cc"],
    extension_names = ["envoy.filters.http.basic_auth"],
    deps = [
        "//source/extensions/filters/http/basic_auth:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
    ],
)

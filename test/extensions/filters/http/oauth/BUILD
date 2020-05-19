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
    name = "oauth_test",
    srcs = ["filter_test.cc"],
    extension_name = "envoy.filters.http.oauth",
    deps = [
        "//source/extensions/filters/http/oauth:oauth_callback_interface",
        "//source/extensions/filters/http/oauth:oauth_client",
        "//source/extensions/filters/http/oauth:oauth_lib",
        "//source/extensions/filters/http/oauth:config",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "oauth_client_test",
    srcs = ["oauth_test.cc"],
    extension_name = "envoy.filters.http.oauth",
    deps = [
        "//source/extensions/filters/http/oauth:oauth_client",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

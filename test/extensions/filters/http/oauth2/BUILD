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
    extension_names = ["envoy.filters.http.oauth2"],
    deps = [
        "//source/extensions/filters/http/oauth2:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/oauth2/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "oauth_integration_test",
    srcs = ["oauth_integration_test.cc"],
    extension_names = ["envoy.filters.http.oauth2"],
    deps = [
        "//source/common/common:base64_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/oauth2:config",
        "//source/extensions/filters/http/oauth2:oauth_lib",
        "//test/integration:http_integration_lib",
        "//test/integration:integration_lib",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_extension_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.oauth2"],
    deps = [
        "//source/common/secret:secret_manager_impl_lib",
        "//source/extensions/filters/http/oauth2:config",
        "//source/extensions/filters/http/oauth2:oauth_callback_interface",
        "//source/extensions/filters/http/oauth2:oauth_client",
        "//source/extensions/filters/http/oauth2:oauth_lib",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/oauth2/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "oauth_client_test",
    srcs = ["oauth_test.cc"],
    extension_names = ["envoy.filters.http.oauth2"],
    deps = [
        "//source/extensions/filters/http/oauth2:oauth_client",
        "//test/integration:http_integration_lib",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

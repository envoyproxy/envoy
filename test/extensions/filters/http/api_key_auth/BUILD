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
    extension_names = ["envoy.filters.http.api_key_auth"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/api_key_auth:api_key_auth_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.api_key_auth"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/api_key_auth:config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.filters.http.api_key_auth"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/api_key_auth:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/api_key_auth/v3:pkg_cc_proto",
    ],
)

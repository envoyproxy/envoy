licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_extension_cc_test",
)

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
        "//test/mocks/server:server_mocks",
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
        "//test/test_common:utility_lib",
    ],
)

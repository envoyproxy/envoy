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
    name = "context_test",
    srcs = ["context_test.cc"],
    extension_name = "envoy.filters.http.rbac",
    deps = [
        "//source/extensions/filters/common/expr:context_lib",
        "//test/mocks/ssl:ssl_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

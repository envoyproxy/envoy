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
    name = "header_to_metadata_filter_test",
    srcs = ["header_to_metadata_filter_test.cc"],
    extension_name = "envoy.filters.http.header_to_metadata",
    deps = [
        "//source/extensions/filters/http/header_to_metadata:header_to_metadata_filter_lib",
        "//test/mocks/server:server_mocks",
    ],
)

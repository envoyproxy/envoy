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
    name = "header_to_metadata_filter_test",
    srcs = ["header_to_metadata_filter_test.cc"],
    extension_name = "envoy.filters.http.header_to_metadata",
    deps = [
        "//source/common/common:base64_lib",
        "//source/extensions/filters/http/header_to_metadata:header_to_metadata_filter_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.header_to_metadata",
    deps = [
        "//source/extensions/filters/http/header_to_metadata:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/header_to_metadata/v3:pkg_cc_proto",
    ],
)

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
    extension_names = ["envoy.filters.thrift.header_to_metadata"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy/filters/header_to_metadata:config",
        "//source/extensions/filters/network/thrift_proxy/filters/header_to_metadata:header_to_metadata_filter_lib",
        "//test/extensions/filters/network/thrift_proxy:mocks",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "header_to_metadata_filter_test",
    srcs = ["header_to_metadata_filter_test.cc"],
    extension_names = ["envoy.filters.thrift.header_to_metadata"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/network/thrift_proxy/filters/header_to_metadata:header_to_metadata_filter_lib",
        "//test/extensions/filters/network/thrift_proxy:mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/ssl:ssl_mocks",
    ],
)

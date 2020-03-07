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
    name = "gzip_filter_test",
    srcs = ["gzip_filter_test.cc"],
    extension_name = "envoy.filters.http.gzip",
    deps = [
        "//source/common/compressor:compressor_lib",
        "//source/common/decompressor:decompressor_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/gzip:config",
        "//source/extensions/filters/http/gzip:gzip_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/gzip/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "gzip_filter_integration_test",
    srcs = [
        "gzip_filter_integration_test.cc",
    ],
    extension_name = "envoy.filters.http.gzip",
    deps = [
        "//source/common/decompressor:decompressor_lib",
        "//source/extensions/filters/http/gzip:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "compressor_filter_test",
    srcs = [
        "compressor_filter_test.cc",
    ],
    extension_name = "envoy.filters.http.compressor",
    deps = [
        "//source/extensions/filters/http/compressor:compressor_filter_lib",
        "//test/mocks/compression/compressor:compressor_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "compressor_filter_integration_test",
    srcs = [
        "compressor_filter_integration_test.cc",
    ],
    extension_name = "envoy.filters.http.compressor",
    tags = ["fails_on_windows"],
    deps = [
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/compression/gzip/decompressor:config",
        "//source/extensions/filters/http/compressor:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "mock_config",
    srcs = ["mock_compressor_library.proto"],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = [
        "config_test.cc",
    ],
    extension_name = "envoy.filters.http.compressor",
    deps = [
        ":mock_config_cc_proto",
        "//source/extensions/filters/http/compressor:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

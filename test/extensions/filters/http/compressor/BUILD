load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_cc_test_library",
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
    extension_names = ["envoy.filters.http.compressor"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/filters/http/compressor:compressor_filter_lib",
        "//test/mocks/compression/compressor:compressor_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "compressor_filter_integration_test",
    size = "large",
    srcs = [
        "compressor_filter_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.compressor"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/compression/gzip/decompressor:config",
        "//source/extensions/filters/http/compressor:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
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
    extension_names = ["envoy.filters.http.compressor"],
    rbe_pool = "2core",
    deps = [
        ":mock_config_cc_proto",
        "//source/extensions/filters/http/compressor:config",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "compressor_filter_speed_test",
    srcs = ["compressor_filter_speed_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//envoy/compression/compressor:compressor_factory_interface",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/compression/brotli/compressor:compressor_lib",
        "//source/extensions/compression/brotli/compressor:config",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/compression/zstd/compressor:config",
        "//source/extensions/filters/http/compressor:compressor_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
        "@com_github_google_benchmark//:benchmark",
        "@com_google_googletest//:gtest",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "compressor_filter_speed_test_benchmark_test",
    benchmark_binary = "compressor_filter_speed_test",
    rbe_pool = "2core",
    # TODO(envoyproxy/windows-dev): diagnose clang-cl build test failure
    tags = ["fails_on_windows"],
)

envoy_cc_test_library(
    name = "compressor_integration_tests_lib",
    hdrs = ["compressor_integration_tests.h"],
)

envoy_cc_test(
    name = "compressor_integration_tests",
    size = "large",
    srcs = [
        "compressor_integration_tests.cc",
    ],
    rbe_pool = "2core",
    deps = [
        ":compressor_integration_tests_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/filters/http/buffer:config",
        "//source/extensions/filters/http/compressor:config",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

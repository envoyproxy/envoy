load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "compressor_filter_test",
    srcs = ["compressor_filter_test.cc"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/compression/gzip/compressor:config",
        "//source/extensions/filters/http/common/compressor:compressor_lib",
        "//test/mocks/compression/compressor:compressor_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

envoy_cc_benchmark_binary(
    name = "compressor_filter_speed_test",
    srcs = ["compressor_filter_speed_test.cc"],
    external_deps = [
        "benchmark",
        "googletest",
    ],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/compression/gzip/compressor:compressor_lib",
        "//source/extensions/filters/http/common/compressor:compressor_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/compressor/v3:pkg_cc_proto",
    ],
)

envoy_benchmark_test(
    name = "compressor_filter_speed_test_benchmark_test",
    benchmark_binary = "compressor_filter_speed_test",
)

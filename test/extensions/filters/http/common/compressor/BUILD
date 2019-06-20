licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "compressor_filter_test",
    srcs = ["compressor_filter_test.cc"],
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common/compressor:compressor_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/filter/http/compressor_common/v2:compressor_common_proto_cc",
    ],
)

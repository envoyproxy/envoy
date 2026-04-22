load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "data_input_test",
    srcs = ["data_input_test.cc"],
    deps = [
        "//envoy/registry",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/network:address_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/matching/http/dynamic_modules:data_input_lib",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

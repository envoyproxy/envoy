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
    name = "inputs_test",
    srcs = ["inputs_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//source/common/router:string_accessor_lib",
        "//source/common/stream_info:stream_info_lib",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_test(
    name = "status_code_input_test",
    srcs = ["status_code_input_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:status_code_input_lib",
        "//source/common/network:address_lib",
        "//source/common/network:socket_lib",
        "//source/common/stream_info:stream_info_lib",
        "//test/test_common:test_time_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "inputs_speed_test",
    srcs = ["inputs_speed_test.cc"],
    rbe_pool = "linux_x64_small",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http/matching:data_impl_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/common/matcher:exact_map_matcher_lib",
        "//source/common/matcher:field_matcher_lib",
        "//source/common/matcher:list_matcher_lib",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/stream_info:stream_info_mocks",
        "@benchmark",
    ],
)

envoy_benchmark_test(
    name = "inputs_speed_test_benchmark_test",
    benchmark_binary = "inputs_speed_test",
)

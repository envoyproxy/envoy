licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_benchmark_test",
    "envoy_cc_benchmark_binary",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:byte_order_lib",
    ],
)

envoy_proto_library(
    name = "buffer_fuzz_proto",
    srcs = ["buffer_fuzz.proto"],
)

envoy_cc_test_library(
    name = "buffer_fuzz_lib",
    srcs = ["buffer_fuzz.cc"],
    hdrs = ["buffer_fuzz.h"],
    deps = [
        ":buffer_fuzz_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/common/memory:stats_lib",
        "//source/common/network:address_lib",
        "//test/fuzz:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "buffer_fuzz_test",
    srcs = ["buffer_fuzz_test.cc"],
    corpus = "buffer_corpus",
    deps = [":buffer_fuzz_lib"],
)

envoy_cc_test(
    name = "buffer_test",
    srcs = ["buffer_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "owned_impl_test",
    srcs = ["owned_impl_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/network:address_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:logging_lib",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "watermark_buffer_test",
    srcs = ["watermark_buffer_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
        "//source/common/network:address_lib",
    ],
)

envoy_cc_test(
    name = "zero_copy_input_stream_test",
    srcs = ["zero_copy_input_stream_test.cc"],
    deps = [
        ":utility_lib",
        "//source/common/buffer:zero_copy_input_stream_lib",
    ],
)

envoy_cc_benchmark_binary(
    name = "buffer_speed_test",
    srcs = ["buffer_speed_test.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
    ],
)

envoy_benchmark_test(
    name = "buffer_speed_test_benchmark_test",
    benchmark_binary = "buffer_speed_test",
)

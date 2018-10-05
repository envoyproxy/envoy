licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
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
        "//source/common/buffer:buffer_lib",
        "//test/mocks/api:api_mocks",
        "//test/test_common:threadsafe_singleton_injector_lib",
    ],
)

envoy_cc_test(
    name = "watermark_buffer_test",
    srcs = ["watermark_buffer_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/buffer:watermark_buffer_lib",
    ],
)

envoy_cc_test(
    name = "zero_copy_input_stream_test",
    srcs = ["zero_copy_input_stream_test.cc"],
    deps = [
        "//source/common/buffer:zero_copy_input_stream_lib",
    ],
)

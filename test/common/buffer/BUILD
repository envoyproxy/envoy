licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "owned_impl_test",
    srcs = ["owned_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
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

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "external_buffer_impl_test",
    srcs = ["external_buffer_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/external_buffer:external_buffer_impl_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "buffer_manager_test",
    srcs = ["buffer_manager_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/external_buffer:buffer_manager_lib",
        "//source/common/external_buffer:external_buffer_impl_lib",
        "//test/mocks/event:event_mocks",
        "//test/test_common:utility_lib",
    ],
)

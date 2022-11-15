load("@envoy//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_package")

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "extra_stream_info_test",
    srcs = ["extra_stream_info_test.cc"],
    repository = "@envoy",
    deps = [
        "//library/common/stream_info:extra_stream_info_lib",
        "@envoy//source/common/stream_info:stream_info_lib",
        "@envoy//test/test_common:simulated_time_system_lib",
    ],
)

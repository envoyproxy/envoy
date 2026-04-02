load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "fair_token_bucket_impl_test",
    srcs = ["fair_token_bucket_impl_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/bandwidth_share:fair_token_bucket_impl_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:test_time_lib",
        "//test/test_common:utility_lib",
    ],
)

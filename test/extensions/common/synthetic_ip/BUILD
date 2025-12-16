load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# Disabled SyntheticIpCacheTest
# envoy_cc_test(
#     name = "cache_test",
#     srcs = ["cache_test.cc"],
#     deps = [
#         "//source/extensions/common/synthetic_ip:cache_lib",
#         "//test/mocks/event:event_mocks",
#         "//test/test_common:simulated_time_system_lib",
#         "//test/test_common:utility_lib",
#     ],
# )

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "proxy_protocol_test",
    srcs = ["proxy_protocol_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/proxy_protocol:proxy_protocol_lib",
        "//test/test_common:utility_lib",
    ],
)

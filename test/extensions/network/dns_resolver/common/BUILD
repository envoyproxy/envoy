load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "fake_udp_dns_server_lib",
    srcs = ["fake_udp_dns_server.cc"],
    hdrs = ["fake_udp_dns_server.h"],
    deps = [
        "//source/common/common:assert_lib",
    ],
)

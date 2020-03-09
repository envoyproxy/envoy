licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "proxy_protocol_common_test",
    srcs = ["proxy_protocol_header_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/common/proxy_protocol:proxy_protocol_header_lib",
        "//test/test_common:utility_lib",
    ],
)

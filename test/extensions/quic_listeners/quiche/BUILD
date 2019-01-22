licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_cc_test_binary",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)

envoy_package()

envoy_cc_test(
    name = "dummy_test",
    srcs = ["dummy_test.cc"],
    external_deps = ["quiche_http2_platform"],
    deps = [
        "//source/extensions/quic_listeners/quiche:dummy_lib",
        "//test/test_common:utility_lib",
    ],
)

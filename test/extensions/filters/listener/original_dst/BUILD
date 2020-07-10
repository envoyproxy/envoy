load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.listener.original_dst",
    deps = [
        "//source/extensions/filters/listener/original_dst:config",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "original_dst_fuzz_test_proto",
    srcs = ["original_dst_fuzz_test.proto"],
)

envoy_cc_fuzz_test(
    name = "original_dst_fuzz_test",
    srcs = ["original_dst_fuzz_test.cc"],
    corpus = "original_dst_corpus",
    deps = [
        ":original_dst_fuzz_test_proto_cc_proto",
        "//source/extensions/filters/listener/original_dst:original_dst_lib",
        "//test/mocks/network:network_mocks",
    ],
)

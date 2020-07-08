load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_extensions",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "network_filter_fuzz_proto",
    srcs = ["network_filter_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/listener/v3:pkg",
    ],
)

envoy_cc_test_library(
    name = "uber_filter_lib",
    srcs = [
        "uber_filter.cc",
    ],
    hdrs = ["uber_filter.h"],
    deps = [
        ":network_filter_fuzz_proto_cc_proto",
        "//source/common/config:utility_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:utility_lib",
        "//source/extensions/filters/network/ext_authz",
        "//test/extensions/filters/common/ext_authz:ext_authz_mocks",
        "//test/extensions/filters/common/ext_authz:ext_authz_test_common",
        "//test/fuzz:utility_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_cc_fuzz_test(
    name = "network_filter_fuzz_test",
    srcs = ["network_filter_fuzz_test.cc"],
    corpus = "network_filter_corpus",
    # All Envoy extensions must be linked to the test in order for the fuzzer to pick
    # these up via the NamedHttpFilterConfigFactory.
    deps = [
        ":uber_filter_lib",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//test/config:utility_lib",
    ] + envoy_all_extensions(),
)

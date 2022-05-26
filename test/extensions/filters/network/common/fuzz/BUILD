load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_all_network_filters",
)

licenses(["notice"])  # Apache 2

exports_files([
    "uber_per_readfilter.cc",
])

envoy_package()

envoy_proto_library(
    name = "network_readfilter_fuzz_proto",
    srcs = ["network_readfilter_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/listener/v3:pkg",
    ],
)

envoy_proto_library(
    name = "network_writefilter_fuzz_proto",
    srcs = ["network_writefilter_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/config/listener/v3:pkg",
    ],
)

envoy_cc_test_library(
    name = "uber_readfilter_lib",
    srcs = [
        "uber_per_readfilter.cc",
        "uber_readfilter.cc",
    ],
    hdrs = ["uber_readfilter.h"],
    deps = [
        ":network_readfilter_fuzz_proto_cc_proto",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/network:well_known_names",
        "//test/extensions/filters/common/ext_authz:ext_authz_test_common",
        "//test/extensions/filters/network/common/fuzz/utils:network_filter_fuzzer_fakes_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/network:network_mocks",
        "@envoy_api//envoy/extensions/filters/network/direct_response/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/local_ratelimit/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/v3:pkg_cc_proto",
    ],
)

envoy_cc_fuzz_test(
    name = "network_readfilter_fuzz_test",
    srcs = ["network_readfilter_fuzz_test.cc"],
    corpus = "network_readfilter_corpus",
    dictionaries = ["network_readfilter_fuzz_test.dict"],
    # All Envoy network filters must be linked to the test in order for the fuzzer to pick
    # these up via the NamedNetworkFilterConfigFactory.
    deps = [
        ":uber_readfilter_lib",
        "//source/common/config:utility_lib",
        "//test/config:utility_lib",
        "//test/test_common:test_runtime_lib",
    ] + envoy_all_network_filters(),
)

envoy_cc_test_library(
    name = "uber_writefilter_lib",
    srcs = [
        "uber_per_writefilter.cc",
        "uber_writefilter.cc",
    ],
    hdrs = ["uber_writefilter.h"],
    deps = [
        ":network_writefilter_fuzz_proto_cc_proto",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network:well_known_names",
        "//test/extensions/filters/network/common/fuzz/utils:network_filter_fuzzer_fakes_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/network:network_mocks",
    ],
)

envoy_cc_fuzz_test(
    name = "network_writefilter_fuzz_test",
    srcs = ["network_writefilter_fuzz_test.cc"],
    corpus = "network_writefilter_corpus",
    # All Envoy network filters must be linked to the test in order for the fuzzer to pick
    # these up via the NamedNetworkFilterConfigFactory.
    deps = [
        ":uber_writefilter_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/filters/network/mongo_proxy:config",
        "//source/extensions/filters/network/zookeeper_proxy:config",
        "//test/config:utility_lib",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test_library",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//source/extensions:all_extensions.bzl",
    "envoy_filters_from_selected",
)
load(
    ":config.bzl",
    "READFILTER_FUZZ_FILTERS",
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
    name = "vig_anymap_ext_lib",
    srcs = ["validated_input_generator_any_map_extensions.cc"],
    hdrs = ["validated_input_generator_any_map_extensions.h"],
    deps = [
        "//source/extensions/access_loggers/file:config",
        "//source/extensions/http/early_header_mutation/header_mutation:config",
        "//source/extensions/http/header_validators/envoy_default:config",
        "//source/extensions/http/original_ip_detection/custom_header:config",
        "//source/extensions/http/original_ip_detection/xff:config",
        "//source/extensions/matching/actions/format_string:config",
        "//source/extensions/matching/common_inputs/environment_variable:config",
        "//source/extensions/matching/input_matchers/consistent_hashing:config",
        "//source/extensions/matching/input_matchers/ip:config",
        "//source/extensions/request_id/uuid:config",
        "//test/fuzz:validated_input_generator_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/trace/v3:pkg_cc_proto",
    ],
)

envoy_cc_test_library(
    name = "uber_readfilter_lib",
    srcs = [
        "uber_per_readfilter.cc",
        "uber_readfilter.cc",
    ],
    hdrs = ["uber_readfilter.h"],
    rbe_pool = "2core",
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
    rbe_pool = "2core",
    deps = [
        ":uber_readfilter_lib",
        ":vig_anymap_ext_lib",
        "//source/common/config:utility_lib",
        "//source/common/http:rds_lib",
        "//test/config:utility_lib",
        "//test/test_common:test_runtime_lib",
        "@com_github_google_libprotobuf_mutator//:libprotobuf_mutator",
    ] + envoy_filters_from_selected(READFILTER_FUZZ_FILTERS),
)

# The filters to be fuzzed by the network_writefilter_fuzz_test. Prefer to add only stable filters,
# that do not have a dedicated fuzzer to them.
WRITEFILTER_FUZZ_FILTERS = [
    "envoy.filters.network.zookeeper_proxy",
    "envoy.filters.network.kafka_broker",
    "envoy.filters.network.mongo_proxy",
    "envoy.filters.network.mysql_proxy",
    # TODO(jianwendong) Add "NetworkFilterNames::get().Postgres" after it
    # supports untrusted data.
]

envoy_cc_test_library(
    name = "uber_writefilter_lib",
    srcs = [
        "uber_per_writefilter.cc",
        "uber_writefilter.cc",
    ],
    hdrs = ["uber_writefilter.h"],
    rbe_pool = "2core",
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
    rbe_pool = "2core",
    # All Envoy network filters must be linked to the test in order for the fuzzer to pick
    # these up via the NamedNetworkFilterConfigFactory.
    deps = [
        ":uber_writefilter_lib",
        ":vig_anymap_ext_lib",
        "//source/common/config:utility_lib",
        "//test/config:utility_lib",
    ] + envoy_filters_from_selected(WRITEFILTER_FUZZ_FILTERS),
)

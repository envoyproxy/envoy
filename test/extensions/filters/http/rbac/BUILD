load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/rbac:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "rbac_filter_test",
    srcs = ["rbac_filter_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/common/matcher:matcher_lib",
        "//source/extensions/common/matcher:trie_matcher_lib",
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//source/extensions/matching/input_matchers/ip:config",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/extensions/filters/common/rbac:engine_mocks",
        "//test/extensions/filters/http/rbac:route_config_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/matching/common_inputs/network/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/matchers/upstream_ip_port/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "rbac_filter_integration_test",
    size = "large",
    srcs = ["rbac_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.rbac"],
    rbe_pool = "2core",
    shard_count = 3,
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/clusters/dynamic_forward_proxy:cluster",
        "//source/extensions/common/matcher:matcher_lib",
        "//source/extensions/common/matcher:trie_matcher_lib",
        "//source/extensions/filters/http/dynamic_forward_proxy:config",
        "//source/extensions/filters/http/rbac:config",
        "//source/extensions/key_value/file_based:config_lib",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//source/extensions/matching/input_matchers/ip:config",
        "//source/extensions/matching/network/common:inputs_lib",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_mock(
    name = "route_config_mocks",
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.http.rbac"],
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/common/rbac:utility_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

envoy_proto_library(
    name = "rbac_filter_fuzz_proto",
    srcs = ["rbac_filter_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg",
    ],
)

envoy_cc_fuzz_test(
    name = "rbac_filter_fuzz_test",
    srcs = ["rbac_filter_fuzz_test.cc"],
    corpus = "rbac_filter_corpus",
    rbe_pool = "2core",
    deps = [
        ":rbac_filter_fuzz_proto_cc_proto",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/rbac:rbac_filter_lib",
        "//test/extensions/filters/http/common/fuzz:http_filter_fuzzer_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
    ],
)

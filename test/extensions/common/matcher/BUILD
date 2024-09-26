load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/common/matcher:matcher_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "trie_matcher_test",
    srcs = ["trie_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/matcher:matcher_lib",
        "//source/common/network:address_lib",
        "//source/common/network/matching:data_impl_lib",
        "//source/common/network/matching:inputs_lib",
        "//source/extensions/common/matcher:trie_matcher_lib",
        "//source/extensions/matching/network/application_protocol:config",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:registry_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_cc_test_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "test_utility_lib",
    hdrs = ["test_utility.h"],
    deps = [
        "//source/common/matcher:matcher_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "list_matcher_test",
    srcs = ["list_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_utility_lib",
        "//source/common/matcher:list_matcher_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "exact_map_matcher_test",
    srcs = ["exact_map_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_utility_lib",
        "//source/common/matcher:exact_map_matcher_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "prefix_map_matcher_test",
    srcs = ["prefix_map_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_utility_lib",
        "//source/common/matcher:prefix_map_matcher_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "field_matcher_test",
    srcs = ["field_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_utility_lib",
        "//source/common/matcher:field_matcher_lib",
        "//source/common/matcher:matcher_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "value_input_matcher_test",
    srcs = ["value_input_matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/common/matcher:value_input_matcher_lib",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "matcher_test",
    srcs = ["matcher_test.cc"],
    rbe_pool = "2core",
    deps = [
        ":test_utility_lib",
        "//source/common/matcher:matcher_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:registry_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/common/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
    ],
)

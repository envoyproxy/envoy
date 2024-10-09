load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test_library(
    name = "cel_matcher_test_lib",
    hdrs = ["cel_matcher_test.h"],
    tags = ["skip_on_windows"],
)

envoy_extension_cc_test(
    name = "cel_matcher_test",
    srcs = ["cel_matcher_test.cc"],
    extension_names = ["envoy.matching.matchers.cel_matcher"],
    rbe_pool = "2core",
    tags = ["skip_on_windows"],
    deps = [
        ":cel_matcher_test_lib",
        "//source/common/matcher:matcher_lib",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:cel_matcher_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/common/matcher:test_utility_lib",
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

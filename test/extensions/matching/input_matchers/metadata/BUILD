load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "dyn_meta_matcher_test",
    srcs = ["dyn_meta_matcher_test.cc"],
    extension_names = ["envoy.matching.matchers.metadata_matcher"],
    rbe_pool = "2core",
    deps = [
        "//source/common/matcher:matcher_lib",
        "//source/extensions/matching/http/metadata_input:metadata_input_lib",
        "//source/extensions/matching/input_matchers/metadata:config",
        "//source/extensions/matching/input_matchers/metadata:metadata_lib",
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

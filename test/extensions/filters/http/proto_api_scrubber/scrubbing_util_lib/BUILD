load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "field_checker_test",
    srcs = ["field_checker_test.cc"],
    data = ["//test/proto:apikeys_proto_descriptor"],
    deps = [
        "//source/common/protobuf",
        "//source/extensions/filters/http/proto_api_scrubber/scrubbing_util_lib",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@com_google_protoprocessinglib//proto_processing_lib/proto_scrubber",
    ],
)

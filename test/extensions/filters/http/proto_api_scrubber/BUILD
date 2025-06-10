load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "filter_config_test",
    srcs = ["filter_config_test.cc"],
    data = [
        "//test/config/integration/certs",
        "//test/proto:apikeys_proto_descriptor",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/matcher:matcher_lib",
        "//source/extensions/common/matcher:matcher_lib",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/common/matcher:test_utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = ["//test/proto:apikeys_proto_descriptor"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/proto_api_scrubber:filter",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/proto:bookstore_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:utility_lib",
    ],
)

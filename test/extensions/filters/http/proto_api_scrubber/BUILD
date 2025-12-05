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
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@com_google_absl//absl/status",
    ],
)

envoy_cc_test(
    name = "filter_test",
    srcs = ["filter_test.cc"],
    data = [
        "//test/proto:apikeys_proto_descriptor",
        "//test/proto:bookstore_proto_descriptor",
    ],
    rbe_pool = "6gig",
    deps = [
        "//source/common/grpc:common_lib",
        "//source/common/grpc:status_lib",
        "//source/common/http/matching:inputs_lib",
        "//source/extensions/filters/http/proto_api_scrubber:filter",
        "//source/extensions/filters/http/proto_api_scrubber:filter_config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/matcher:matcher_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/proto:apikeys_proto_cc_proto",
        "//test/proto:bookstore_proto_cc_proto",
        "//test/test_common:environment_lib",
        "//test/test_common:logging_lib",
        "//test/test_common:utility_lib",
        "@com_google_absl//absl/log",
        "@com_google_absl//absl/strings",
    ],
)

envoy_cc_test(
    name = "integration_test",
    size = "large",
    srcs = [
        "integration_test.cc",
    ],
    data = ["//test/proto:apikeys_proto_descriptor"],
    # Following attribute will be enabled once the filter is registered.
    # extension_names = ["envoy.filters.http.proto_api_scrubber"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/grpc:common_lib",
        "//source/extensions/filters/http/proto_api_scrubber:config",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/extensions/filters/http/grpc_field_extraction/message_converter:message_converter_test_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/proto:apikeys_proto_cc_proto",
        "@com_google_absl//absl/strings:str_format",
        "@com_google_cel_cpp//parser",
        "@envoy_api//envoy/extensions/filters/http/proto_api_scrubber/v3:pkg_cc_proto",
    ],
)

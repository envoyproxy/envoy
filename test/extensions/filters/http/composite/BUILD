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
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.filters.http.composite"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/composite:config",
        "//source/extensions/filters/http/composite:filter_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/http:http_mocks",
    ],
)

envoy_extension_cc_test(
    name = "composite_filter_integration_test",
    size = "large",
    srcs = ["composite_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.composite"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http/match_delegate:config",
        "//source/extensions/filters/http/composite:config",
        "//source/extensions/filters/http/composite:filter_lib",
        "//source/extensions/matching/http/cel_input:cel_input_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:cel_matcher_lib",
        "//source/extensions/matching/input_matchers/cel_matcher:config",
        "//test/common/grpc:grpc_client_integration_lib",
        "//test/common/http:common_lib",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:server_factory_context_filter_config_proto_cc_proto",
        "//test/integration/filters:server_factory_context_filter_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/proto:helloworld_proto_cc_proto",
        "@com_github_cncf_xds//xds/type/matcher/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/matching/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/composite/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

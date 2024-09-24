load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/match_delegate:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
    ],
)

envoy_cc_test(
    name = "match_delegate_integration_test",
    srcs = ["match_delegate_integration_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/match_delegate:config",
        "//source/server/config_validation:server_lib",
        "//test/integration:http_integration_lib",
        "//test/integration/filters:set_response_code_filter_config_proto_cc_proto",
        "//test/integration/filters:set_response_code_filter_lib",
        "//test/mocks/server:options_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/matching/v3:pkg_cc_proto",
    ],
)

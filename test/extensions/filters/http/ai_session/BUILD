load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "ai_session_test",
    srcs = ["ai_session_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/ai_session:ai_filter_chain_lib",
        "//source/extensions/filters/http/ai_session:ai_filter_lib",
        "//source/extensions/filters/http/ai_session:ai_session_lib",
        "//source/extensions/filters/http/ai_session:ai_session_manager_lib",
        "//source/extensions/filters/http/ai_session:example_filters_lib",
        "//source/extensions/filters/http/json_rpc:json_rpc_parser_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@nlohmann_json//:json",
    ],
)

envoy_cc_test(
    name = "ai_session_integration_test",
    srcs = ["ai_session_integration_test.cc"],
    deps = [
        "//source/extensions/filters/http/ai_session:config",
        "//test/integration:http_integration_lib",
    ],
)

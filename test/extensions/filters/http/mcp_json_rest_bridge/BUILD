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
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "mcp_json_rest_bridge_filter_test",
    srcs = ["mcp_json_rest_bridge_filter_test.cc"],
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:mcp_json_rest_bridge_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@nlohmann_json//:json",
    ],
)

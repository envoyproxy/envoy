load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "mcp_filter_test",
    srcs = ["mcp_filter_test.cc"],
    deps = [
        "//source/extensions/filters/http/mcp:mcp_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "mcp_filter_integration_test",
    srcs = ["mcp_filter_integration_test.cc"],
    deps = [
        "//source/extensions/filters/http/mcp:config",
        "//test/integration:http_integration_lib",
    ],
)

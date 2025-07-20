load("//bazel:envoy_build_system.bzl", "envoy_cc_test", "envoy_contrib_package")

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "mcp_sse_test",
    srcs = ["mcp_sse_test.cc"],
    deps = [
        "//contrib/mcp_sse_stateful_session/filters/http/source:mcp_sse_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
    ],
)

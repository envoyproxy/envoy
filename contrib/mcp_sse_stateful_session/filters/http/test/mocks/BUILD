load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_mock(
    name = "mcp_sse_stateful_session_mock",
    srcs = ["mcp_sse_stateful_session.cc"],
    hdrs = ["mcp_sse_stateful_session.h"],
    deps = [
        "//contrib/mcp_sse_stateful_session/http/source:mcp_sse_stateful_session_interface",
    ],
)
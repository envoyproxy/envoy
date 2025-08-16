load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_mock",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_mock(
    name = "mcp_sse_stateful_session_mock",
    srcs = ["mcp_sse_stateful_session.cc"],
    hdrs = ["mcp_sse_stateful_session.h"],
    deps = [
        "//envoy/http:mcp_sse_stateful_session_interface",
        "//source/common/protobuf",
    ],
)

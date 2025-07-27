load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "envelope_test",
    srcs = ["envelope_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//source/common/http:utility_lib",
        "//contrib/mcp_sse_stateful_session/http/source:envelope_lib",
        "//test/mocks/upstream:host_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//envoy/registry",
        "//contrib/mcp_sse_stateful_session/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)
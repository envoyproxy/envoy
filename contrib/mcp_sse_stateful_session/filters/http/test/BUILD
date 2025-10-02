load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "stateful_session_test",
    srcs = ["stateful_session_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//contrib/mcp_sse_stateful_session/filters/http/source:config",
        "//contrib/mcp_sse_stateful_session/filters/http/test/mocks:mcp_sse_stateful_session_mock",
        "//source/server:generic_factory_context_lib",
        "//test/mocks/api:api_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "stateful_session_integration_test",
    size = "large",
    srcs = [
        "stateful_session_integration_test.cc",
    ],
    rbe_pool = "6gig",
    deps = [
        "//contrib/mcp_sse_stateful_session/filters/http/source:config",
        "//contrib/mcp_sse_stateful_session/http/source:config",
        "//source/common/protobuf",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "6gig",
    deps = [
        "//contrib/mcp_sse_stateful_session/filters/http/source:config",
        "//contrib/mcp_sse_stateful_session/filters/http/test/mocks:mcp_sse_stateful_session_mock",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
    ],
)

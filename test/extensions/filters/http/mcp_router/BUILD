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
    name = "session_codec_test",
    srcs = ["session_codec_test.cc"],
    extension_names = ["envoy.filters.http.mcp_router"],
    deps = [
        "//source/extensions/filters/http/mcp_router:session_codec_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "mcp_router_test",
    srcs = ["mcp_router_test.cc"],
    extension_names = ["envoy.filters.http.mcp_router"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/mcp_router:mcp_router_lib",
        "//test/mocks/event:event_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "mcp_router_integration_test",
    size = "large",
    srcs = ["mcp_router_integration_test.cc"],
    extension_names = ["envoy.filters.http.mcp_router"],
    rbe_pool = "6gig",
    deps = [
        "//source/extensions/filters/http/mcp:config",
        "//source/extensions/filters/http/mcp_router:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/mcp_router/v3:pkg_cc_proto",
    ],
)

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
    name = "filter_test",
    srcs = ["filter_test.cc"],
    extension_names = ["envoy.http.ai_filters.mcp"],
    deps = [
        "//source/extensions/filters/common/mcp:filter_state_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_request_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_lib",
        "//source/extensions/filters/http/mcp:mcp_filter_lib",
        "//source/extensions/http/ai_filters/mcp:filter_lib",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "parity_test",
    srcs = ["parity_test.cc"],
    extension_names = ["envoy.http.ai_filters.mcp"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/common/mcp:filter_state_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/ai_protocol_manager:ai_request_lib",
        "//source/extensions/filters/http/ai_protocol_manager:json_with_ext_buf_parser_lib",
        "//source/extensions/filters/http/mcp:mcp_filter_lib",
        "//source/extensions/http/ai_filters/mcp:config",
        "//source/extensions/http/ai_filters/mcp:filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.ai_filters.mcp"],
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//envoy/registry",
        "//source/extensions/filters/http/ai_protocol_manager:ai_filter_lib",
        "//source/extensions/filters/http/mcp:mcp_filter_lib",
        "//source/extensions/http/ai_filters/mcp:config",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/stream_info:stream_info_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "mcp_integration_test",
    size = "large",
    srcs = ["mcp_integration_test.cc"],
    extension_names = [
        "envoy.http.ai_filters.mcp",
        "envoy.filters.http.ai_protocol_manager",
    ],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "cto": {"link": "linux_x64_small"},
        "openssl": {"link": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
        "asan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/ai_protocol_manager:config",
        "//source/extensions/http/ai_filters/mcp:config",
        "//test/integration:fake_access_log_lib",
        "//test/integration:http_integration_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@abseil-cpp//absl/strings",
        "@envoy_api//envoy/extensions/filters/http/ai_protocol_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
    ],
)

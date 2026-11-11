load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_fuzz_test",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "mcp_json_rest_bridge_filter_test",
    srcs = ["mcp_json_rest_bridge_filter_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    rbe_pool = {"build": "linux_x64_small"},
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:mcp_json_rest_bridge_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_json_rest_bridge/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
        "@ocp-diag-core//ocpdiag/core/testing:status_matchers",
    ],
)

envoy_extension_cc_test(
    name = "endpoint_keyed_filtering_test",
    srcs = ["endpoint_keyed_filtering_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    rbe_pool = {"gcc": {"build": "linux_x64_small"}},
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:mcp_json_rest_bridge_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_json_rest_bridge/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
        "@ocp-diag-core//ocpdiag/core/testing:status_matchers",
    ],
)

envoy_extension_cc_test(
    name = "http_request_builder_test",
    srcs = ["http_request_builder_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:http_request_builder_lib",
        "//test/test_common:status_utility_lib",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "mcp_json_rest_bridge_integration_test",
    srcs = ["mcp_json_rest_bridge_integration_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    rbe_pool = {
        "asan": {"build": "linux_x64_small"},
        "tsan": {"build": "linux_x64_small"},
        "gcc": {"build": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:config",
        "//test/integration:fake_access_log_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:registry_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_json_rest_bridge/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "trace_context_test",
    srcs = ["trace_context_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:trace_context_lib",
        "@nlohmann_json//:json",
    ],
)

envoy_extension_cc_test(
    name = "sse_response_extractor_test",
    srcs = ["sse_response_extractor_test.cc"],
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:sse_response_extractor_lib",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_proto_library(
    name = "mcp_json_rest_bridge_fuzz_proto",
    srcs = ["mcp_json_rest_bridge_fuzz.proto"],
    deps = [
        "//test/fuzz:common_proto",
        "@envoy_api//envoy/extensions/filters/http/mcp_json_rest_bridge/v3:pkg",
    ],
)

envoy_extension_cc_fuzz_test(
    name = "mcp_json_rest_bridge_fuzz_test",
    srcs = ["mcp_json_rest_bridge_fuzz_test.cc"],
    corpus = "mcp_json_rest_bridge_corpus",
    extension_names = ["envoy.filters.http.mcp_json_rest_bridge"],
    rbe_pool = {"gcc": {"build": "linux_x64_small"}},
    deps = [
        ":mcp_json_rest_bridge_fuzz_proto_cc_proto",
        "//source/common/common:base64_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/mcp_json_rest_bridge:bridge_status_lib",
        "//source/extensions/filters/http/mcp_json_rest_bridge:mcp_json_rest_bridge_filter_lib",
        "//test/extensions/filters/http/common/fuzz:http_filter_fuzzer_lib",
        "//test/fuzz:utility_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_json_rest_bridge/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
        "@protobuf//third_party/utf8_range:utf8_validity",
        "@re2",
    ],
)

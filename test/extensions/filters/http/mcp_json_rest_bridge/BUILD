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
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "mcp_json_rest_bridge_filter_test",
    srcs = ["mcp_json_rest_bridge_filter_test.cc"],
    rbe_pool = {
        "gcc": {"build": "linux_x64_small"},
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:mcp_json_rest_bridge_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp_json_rest_bridge/v3:pkg_cc_proto",
        "@nlohmann_json//:json",
        "@ocp-diag-core//ocpdiag/core/testing:parse_text_proto",
    ],
)

envoy_cc_test(
    name = "http_request_builder_test",
    srcs = ["http_request_builder_test.cc"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:http_request_builder_lib",
        "//test/test_common:status_utility_lib",
        "@nlohmann_json//:json",
        "@ocp-diag-core//ocpdiag/core/testing:parse_text_proto",
    ],
)

envoy_cc_test(
    name = "mcp_json_rest_bridge_integration_test",
    srcs = ["mcp_json_rest_bridge_integration_test.cc"],
    # linux_x64_xlarge: the coverage link peaks at 15175MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "openssl": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_xlarge",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
        "asan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
        },
    },
    deps = [
        "//source/extensions/filters/http/mcp_json_rest_bridge:config",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@nlohmann_json//:json",
    ],
)

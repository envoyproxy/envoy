load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_fuzz_test",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "mcp_filter_test",
    srcs = ["mcp_filter_test.cc"],
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
        "//source/extensions/filters/http/mcp:mcp_filter_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
    ],
)

envoy_cc_test(
    name = "mcp_json_parser_test",
    srcs = ["mcp_json_parser_test.cc"],
    rbe_pool = {
        "coverage": {"link": "linux_x64_large"},
        "tsan": {"link": "linux_x64_small"},
    },
    deps = [
        "//source/extensions/filters/http/mcp:mcp_json_parser_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_cc_test(
    name = "mcp_filter_integration_test",
    srcs = ["mcp_filter_integration_test.cc"],
    # linux_x64_xlarge: the coverage link peaks at 14955MB,
    # over linux_x64_large (12288MiB) with headroom.
    rbe_pool = {
        "default": {"build": "linux_x64_small"},
        "cto": {
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
        "//source/extensions/filters/http/mcp:config",
        "//test/integration:http_integration_lib",
        "@envoy_api//envoy/extensions/filters/http/mcp/v3:pkg_cc_proto",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = {
        "default": {
            "build": "linux_x64_small",
            "test": "linux_x64_medium",
        },
        "coverage": {
            "build": "linux_x64_small",
            "link": "linux_x64_large",
            "test": "linux_x64_medium",
        },
        "tsan": {
            "build": "linux_x64_small",
            "link": "linux_x64_small",
            "test": "linux_x64_medium",
        },
    },
    deps = [
        "//source/extensions/filters/http/mcp:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_fuzz_test(
    name = "mcp_parser_fuzz_test",
    srcs = ["mcp_parser_fuzz_test.cc"],
    corpus = "mcp_parser_corpus",
    rbe_pool = {"fuzz_coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/filters/http/mcp:mcp_json_parser_lib",
        "//test/test_common:utility_lib",
    ],
)

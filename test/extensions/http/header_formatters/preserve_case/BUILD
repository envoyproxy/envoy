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
    name = "preserve_case_formatter_test",
    srcs = [
        "preserve_case_formatter_test.cc",
    ],
    extension_names = ["envoy.http.stateful_header_formatters.preserve_case"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "preserve_case_formatter_integration_test",
    size = "large",
    srcs = [
        "preserve_case_formatter_integration_test.cc",
    ],
    extension_names = ["envoy.http.stateful_header_formatters.preserve_case"],
    # linux_x64_xlarge: the coverage link peaks at 15053MB,
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
    # Broken until bazel 5.0.0 fix to shorten resulting paths for SymInitialize() failure
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/http/header_formatters/preserve_case:config",
        "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_formatters/preserve_case/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "preserve_case_formatter_reason_phrase_integration_test",
    size = "large",
    srcs = [
        "preserve_case_formatter_reason_phrase_integration_test.cc",
    ],
    extension_names = ["envoy.http.stateful_header_formatters.preserve_case"],
    # linux_x64_xlarge: the coverage link peaks at 15095MB,
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
        "//source/extensions/http/header_formatters/preserve_case:config",
        "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "//test/integration:http_integration_lib",
        "//test/integration:http_protocol_integration_lib",
        "//test/test_common:test_runtime_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/http/header_formatters/preserve_case/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.http.stateful_header_formatters.preserve_case"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//envoy/registry",
        "//source/common/common:utility_lib",
        "//source/extensions/http/header_formatters/preserve_case:config",
        "//source/extensions/http/header_formatters/preserve_case:preserve_case_formatter",
        "//test/test_common:utility_lib",
    ],
)

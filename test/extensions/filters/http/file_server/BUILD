load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "absl_status_to_http_status_test",
    srcs = ["absl_status_to_http_status_test.cc"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    deps = [
        "//source/extensions/filters/http/file_server:absl_status_to_http_status",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "file_server_test",
    srcs = [
        "config_test.cc",
        "filter_test.cc",
    ],
    extension_names = ["envoy.filters.http.file_server"],
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
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/file_server:config",
        "//test/extensions/common/async_files:mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "file_server_integration_test",
    srcs = [
        "integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.file_server"],
    # linux_x64_xlarge: the coverage link peaks at 15858MB,
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
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/file_server:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

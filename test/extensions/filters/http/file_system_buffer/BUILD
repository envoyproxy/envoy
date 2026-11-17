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
    name = "fragment_test",
    srcs = ["fragment_test.cc"],
    extension_names = ["envoy.filters.http.file_system_buffer"],
    rbe_pool = {"coverage": {"link": "linux_x64_small"}},
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/file_system_buffer:fragment",
        "//test/extensions/common/async_files:mocks",
        "//test/mocks/buffer:buffer_mocks",
        "//test/test_common:status_utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "file_system_buffer_test",
    srcs = [
        "config_test.cc",
        "filter_test.cc",
    ],
    extension_names = ["envoy.filters.http.file_system_buffer"],
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
        "//source/extensions/filters/http/file_system_buffer:config",
        "//test/extensions/common/async_files:mocks",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "file_system_buffer_integration_test",
    size = "large",
    srcs = [
        "filter_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.file_system_buffer"],
    # linux_x64_xlarge: the coverage link peaks at 15187MB,
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
    shard_count = 4,
    tags = [
        "cpu:3",
        "skip_on_windows",
    ],
    deps = [
        "//source/extensions/filters/http/file_system_buffer:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

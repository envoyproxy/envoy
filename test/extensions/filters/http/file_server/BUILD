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
    deps = [
        "//source/extensions/filters/http/file_server:absl_status_to_http_status",
    ],
)

envoy_extension_cc_test(
    name = "file_server_test",
    srcs = [
        "config_test.cc",
        "filter_test.cc",
    ],
    extension_names = ["envoy.filters.http.file_server"],
    rbe_pool = "6gig",
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
    rbe_pool = "6gig",
    tags = ["skip_on_windows"],
    deps = [
        "//source/extensions/filters/http/file_server:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

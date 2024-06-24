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
    extension_names = ["envoy.filters.http.json_to_metadata"],
    deps = [
        "//source/extensions/filters/http/json_to_metadata:json_to_metadata_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/server:server_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.json_to_metadata"],
    deps = [
        "//source/extensions/filters/http/json_to_metadata:config",
        "//test/mocks/server:server_mocks",
    ],
)

envoy_extension_cc_test(
    name = "integration_test",
    size = "large",
    srcs = ["integration_test.cc"],
    extension_names = ["envoy.filters.http.json_to_metadata"],
    deps = [
        "//source/extensions/filters/http/json_to_metadata:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

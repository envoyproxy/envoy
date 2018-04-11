licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

envoy_package()

envoy_extension_cc_test(
    name = "buffer_filter_test",
    srcs = ["buffer_filter_test.cc"],
    extension_name = "envoy.filters.http.buffer",
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/http:header_map_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/buffer:buffer_filter_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/upstream:upstream_mocks",
    ],
)

envoy_extension_cc_test(
    name = "buffer_filter_integration_test",
    srcs = ["buffer_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.buffer",
    deps = [
        "//source/extensions/filters/http/buffer:config",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.buffer",
    deps = [
        "//source/extensions/filters/http/buffer:config",
        "//test/mocks/server:server_mocks",
    ],
)

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
    name = "fault_filter_test",
    srcs = ["fault_filter_test.cc"],
    extension_name = "envoy.filters.http.fault",
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/config:filter_json_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/fault:fault_filter_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.fault",
    deps = [
        "//source/extensions/filters/http/fault:config",
        "//test/mocks/server:server_mocks",
    ],
)

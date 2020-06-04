load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test_library",
    "envoy_package",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_test",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_extension_cc_test(
    name = "fault_filter_test",
    srcs = ["fault_filter_test.cc"],
    extension_name = "envoy.filters.http.fault",
    deps = [
        ":utility_lib",
        "//include/envoy/event:dispatcher_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/fault:fault_filter_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.fault",
    deps = [
        ":utility_lib",
        "//source/extensions/filters/http/fault:config",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "fault_filter_integration_test",
    srcs = ["fault_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.fault",
    tags = ["fails_on_windows"],
    deps = [
        "//source/extensions/filters/http/fault:config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

envoy_cc_test_library(
    name = "utility_lib",
    hdrs = ["utility.h"],
    deps = [
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
    ],
)

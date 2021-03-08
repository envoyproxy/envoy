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
    name = "kill_request_filter_test",
    srcs = ["kill_request_filter_test.cc"],
    extension_name = "envoy.filters.http.kill_request",
    deps = [
        "//include/envoy/http:metadata_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/kill_request:kill_request_filter_lib",
        "//test/mocks:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/kill_request/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "kill_request_config_test",
    srcs = ["kill_request_config_test.cc"],
    extension_name = "envoy.filters.http.kill_request",
    deps = [
        "//source/extensions/filters/http/kill_request:kill_request_config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/kill_request/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "kill_request_filter_integration_test",
    srcs = ["kill_request_filter_integration_test.cc"],
    extension_name = "envoy.filters.http.kill_request",
    deps = [
        "//source/extensions/filters/http/kill_request:kill_request_config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

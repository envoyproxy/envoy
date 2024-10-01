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

envoy_extension_cc_test(
    name = "kill_request_filter_test",
    srcs = ["kill_request_filter_test.cc"],
    extension_names = ["envoy.filters.http.kill_request"],
    rbe_pool = "2core",
    deps = [
        "//envoy/http:metadata_interface",
        "//source/common/buffer:buffer_lib",
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
    extension_names = ["envoy.filters.http.kill_request"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/kill_request:kill_request_config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/kill_request/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "kill_request_filter_integration_test",
    size = "large",
    srcs = ["kill_request_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.kill_request"],
    rbe_pool = "2core",
    shard_count = 8,
    deps = [
        "//source/extensions/filters/http/kill_request:kill_request_config",
        "//test/integration:http_protocol_integration_lib",
    ],
)

envoy_cc_test(
    name = "crash_integration_test",
    size = "large",
    srcs = ["crash_integration_test.cc"],
    coverage = False,
    rbe_pool = "2core",
    shard_count = 8,
    deps = [
        "//source/extensions/filters/http/kill_request:kill_request_config",
        "//test/integration:http_protocol_integration_lib",
        "//test/integration/filters:stop_and_continue_filter_config_proto_cc_proto",
        "//test/integration/filters:stop_iteration_and_continue",
        "@envoy_api//envoy/extensions/filters/http/kill_request/v3:pkg_cc_proto",
    ],
)

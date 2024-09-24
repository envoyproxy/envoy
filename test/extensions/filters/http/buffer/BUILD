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
    name = "buffer_filter_test",
    srcs = ["buffer_filter_test.cc"],
    extension_names = ["envoy.filters.http.buffer"],
    rbe_pool = "2core",
    deps = [
        "//envoy/event:dispatcher_interface",
        "//source/common/http:header_map_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/http/buffer:buffer_filter_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/init:init_mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/thread_local:thread_local_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "buffer_filter_integration_test",
    size = "large",
    srcs = ["buffer_filter_integration_test.cc"],
    extension_names = ["envoy.filters.http.buffer"],
    rbe_pool = "2core",
    shard_count = 4,
    deps = [
        "//source/extensions/filters/http/buffer:config",
        "//test/config:utility_lib",
        "//test/integration:http_protocol_integration_lib",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.buffer"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/buffer:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/mocks/server:instance_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
    ],
)

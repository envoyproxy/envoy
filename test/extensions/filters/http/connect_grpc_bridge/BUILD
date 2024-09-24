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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.connect_grpc_bridge"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/connect_grpc_bridge:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/connect_grpc_bridge/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "connect_grpc_bridge_filter_test",
    srcs = ["connect_grpc_bridge_filter_test.cc"],
    extension_names = ["envoy.filters.http.connect_grpc_bridge"],
    rbe_pool = "2core",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/connect_grpc_bridge:filter_lib",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:global_lib",
        "//test/test_common:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "connect_grpc_bridge_integration_test",
    size = "large",
    srcs = [
        "connect_grpc_bridge_integration_test.cc",
    ],
    extension_names = ["envoy.filters.http.connect_grpc_bridge"],
    rbe_pool = "2core",
    deps = [
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/connect_grpc_bridge:config",
        "//source/extensions/filters/http/health_check:config",
        "//test/integration:http_integration_lib",
        "//test/proto:helloworld_proto_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "end_stream_response_test",
    srcs = ["end_stream_response_test.cc"],
    extension_names = ["envoy.filters.http.connect_grpc_bridge"],
    rbe_pool = "2core",
    deps = [
        "//source/extensions/filters/http/connect_grpc_bridge:end_stream_response_lib",
        "//test/test_common:global_lib",
        "//test/test_common:utility_lib",
    ],
)

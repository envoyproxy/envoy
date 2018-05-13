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
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.http_connection_manager",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/config:filter_json_lib",
        "//source/common/event:dispatcher_lib",
        "//source/extensions/filters/http/dynamo:config",
        "//source/extensions/filters/network/http_connection_manager:config",
        "//test/mocks/network:network_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
    ],
)

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
    name = "bson_impl_test",
    srcs = ["bson_impl_test.cc"],
    extension_name = "envoy.filters.network.mongo_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
    ],
)

envoy_extension_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    extension_name = "envoy.filters.network.mongo_proxy",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
        "//source/extensions/filters/network/mongo_proxy:codec_lib",
    ],
)

envoy_extension_cc_test(
    name = "proxy_test",
    srcs = ["proxy_test.cc"],
    extension_name = "envoy.filters.network.mongo_proxy",
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
        "//source/extensions/filters/network/mongo_proxy:codec_lib",
        "//source/extensions/filters/network/mongo_proxy:proxy_lib",
        "//test/common/stream_info:test_util",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/stats:stats_mocks",
        "@envoy_api//envoy/extensions/filters/common/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    extension_name = "envoy.filters.network.mongo_proxy",
    deps = [
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
        "//source/extensions/filters/network/mongo_proxy:codec_lib",
        "//source/extensions/filters/network/mongo_proxy:utility_lib",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.network.mongo_proxy",
    deps = [
        "//source/extensions/filters/network/mongo_proxy:config",
        "//test/mocks/server:server_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/mongo_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

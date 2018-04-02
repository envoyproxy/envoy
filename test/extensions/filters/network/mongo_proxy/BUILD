licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

envoy_package()

envoy_cc_test(
    name = "bson_impl_test",
    srcs = ["bson_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
    ],
)

envoy_cc_test(
    name = "codec_impl_test",
    srcs = ["codec_impl_test.cc"],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/json:json_loader_lib",
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
        "//source/extensions/filters/network/mongo_proxy:codec_lib",
    ],
)

envoy_cc_test(
    name = "proxy_test",
    srcs = ["proxy_test.cc"],
    deps = [
        "//source/common/event:dispatcher_lib",
        "//source/common/stats:stats_lib",
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
        "//source/extensions/filters/network/mongo_proxy:codec_lib",
        "//source/extensions/filters/network/mongo_proxy:proxy_lib",
        "//test/mocks/access_log:access_log_mocks",
        "//test/mocks/event:event_mocks",
        "//test/mocks/filesystem:filesystem_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/runtime:runtime_mocks",
    ],
)

envoy_cc_test(
    name = "utility_test",
    srcs = ["utility_test.cc"],
    deps = [
        "//source/extensions/filters/network/mongo_proxy:bson_lib",
        "//source/extensions/filters/network/mongo_proxy:codec_lib",
        "//source/extensions/filters/network/mongo_proxy:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    deps = [
        "//source/extensions/filters/network/mongo_proxy:config",
        "//test/mocks/server:server_mocks",
    ],
)

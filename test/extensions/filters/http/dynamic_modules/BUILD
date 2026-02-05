load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/dynamic_modules/test_data/c:http_filter_per_route_config_new_fail",
        "//test/extensions/dynamic_modules/test_data/c:no_op",
    ],
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:base64_lib",
        "//source/common/common:hex_lib",
        "//source/common/crypto:utility_lib",
        "//source/common/http:message_lib",
        "//source/common/stats:isolated_store_lib",
        "//source/extensions/dynamic_modules:module_cache_lib",
        "//source/extensions/filters/http/dynamic_modules:factory_lib",
        "//test/extensions/dynamic_modules:util",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/mocks/protobuf:protobuf_mocks",
        "//test/mocks/server:server_mocks",
        "//test/test_common:environment_lib",
        "//test/test_common:simulated_time_system_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/dynamic_modules/v3:pkg_cc_proto",
    ],
)

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
    name = "ratelimit_test",
    srcs = ["ratelimit_test.cc"],
    extension_name = "envoy.filters.thrift.ratelimit",
    deps = [
        "//source/common/buffer:buffer_lib",
        "//source/common/common:empty_string",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/network/thrift_proxy/filters/ratelimit:ratelimit_lib",
        "//test/extensions/filters/common/ratelimit:ratelimit_mocks",
        "//test/extensions/filters/network/thrift_proxy:mocks",
        "//test/mocks/local_info:local_info_mocks",
        "//test/mocks/ratelimit:ratelimit_mocks",
        "//test/mocks/runtime:runtime_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.thrift.ratelimit",
    deps = [
        "//source/extensions/filters/network/thrift_proxy/filters/ratelimit:config",
        "//test/extensions/filters/network/thrift_proxy:mocks",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/thrift_proxy/filters/ratelimit/v3:pkg_cc_proto",
    ],
)

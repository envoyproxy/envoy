load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_test",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

envoy_cc_test(
    name = "checksum_filter_test",
    srcs = ["checksum_filter_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/checksum/filters/http/source:checksum_filter_lib",
        "//source/common/buffer:buffer_lib",
        "//source/common/http:header_map_lib",
        "//test/mocks/http:http_mocks",
        "//test/mocks/server:server_factory_context_mocks",
        "//test/mocks/upstream:upstream_mocks",
        "//test/test_common:utility_lib",
    ],
)

envoy_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    rbe_pool = "2core",
    deps = [
        "//contrib/checksum/filters/http/source:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//contrib/envoy/extensions/filters/http/checksum/v3alpha:pkg_cc_proto",
    ],
)

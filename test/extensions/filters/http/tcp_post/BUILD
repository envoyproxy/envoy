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
    name = "tcp_post_filter_test",
    srcs = ["tcp_post_filter_test.cc"],
    extension_name = "envoy.filters.http.tcp_post",
    deps = [
        "//include/envoy/http:metadata_interface",
        "//source/common/buffer:buffer_lib",
        "//source/extensions/filters/http/tcp_post:tcp_post_filter_lib",
        "//test/mocks:common_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/tcp_post/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    extension_name = "envoy.filters.http.tcp_post",
    deps = [
        "//source/extensions/filters/http/tcp_post:config",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/filters/http/tcp_post/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

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
    extension_name = "envoy.filters.http.original_src",
    deps = [
        "//source/extensions/filters/http/original_src:config_lib",
        "@envoy_api//envoy/extensions/filters/http/original_src/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "original_src_config_factory_test",
    srcs = ["original_src_config_factory_test.cc"],
    extension_name = "envoy.filters.http.original_src",
    deps = [
        "//source/extensions/filters/http/original_src:config",
        "//source/extensions/filters/http/original_src:config_lib",
        "//source/extensions/filters/http/original_src:original_src_lib",
        "//test/mocks/server:server_mocks",
        "@envoy_api//envoy/extensions/filters/http/original_src/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "original_src_test",
    srcs = ["original_src_test.cc"],
    extension_name = "envoy.filters.http.original_src",
    deps = [
        "//source/common/network:socket_option_lib",
        "//source/extensions/filters/http/original_src:original_src_lib",
        "//test/mocks:common_lib",
        "//test/mocks/buffer:buffer_mocks",
        "//test/mocks/http:http_mocks",
        "//test/mocks/network:network_mocks",
        "//test/test_common:printers_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/original_src/v3:pkg_cc_proto",
    ],
)

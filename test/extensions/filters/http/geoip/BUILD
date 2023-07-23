load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
    "envoy_extension_cc_test_library",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_proto_library(
    name = "dummy",
    srcs = ["dummy.proto"],
)

envoy_extension_cc_test(
    name = "config_test",
    size = "small",
    srcs = ["config_test.cc"],
    extension_names = ["envoy.filters.http.geoip"],
    deps = [
        ":geoip_mocks",
        ":utils_lib",
        "//source/common/http:message_lib",
        "//source/extensions/filters/http/geoip:config",
        "//test/mocks/server:factory_context_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/geoip/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "geoip_filter_test",
    size = "small",
    srcs = ["geoip_filter_test.cc"],
    extension_names = ["envoy.filters.http.geoip"],
    deps = [
        ":geoip_mocks",
        "//source/extensions/filters/http/geoip:config",
        "//source/extensions/filters/http/geoip:geoip_filter_lib",
        "//test/common/http:common_lib",
        "//test/mocks/http:http_mocks",
        "//test/test_common:registry_lib",
        "//test/test_common:test_runtime_lib",
        "@envoy_api//envoy/extensions/filters/http/geoip/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_mock(
    name = "geoip_mocks",
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.http.geoip"],
    deps = [
        ":dummy_cc_proto",
        "//source/extensions/filters/http/geoip:provider_config",
    ],
)

envoy_extension_cc_test_library(
    name = "utils_lib",
    hdrs = ["utils.h"],
    extension_names = ["envoy.filters.http.geoip"],
    deps = ["//source/extensions/filters/http/geoip:geoip_filter_lib"],
)

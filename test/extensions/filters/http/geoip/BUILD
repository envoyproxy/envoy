load(
    "//bazel:envoy_build_system.bzl",
    "envoy_package",
    "envoy_proto_library",
)
load(
    "//test/extensions:extensions_build_system.bzl",
    "envoy_extension_cc_mock",
    "envoy_extension_cc_test",
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

envoy_extension_cc_test(
    name = "geoip_filter_integration_test",
    size = "large",
    srcs = ["geoip_filter_integration_test.cc"],
    data = [
        "//test/extensions/geoip_providers/maxmind/test_data:geolocation_databases",
    ],
    extension_names = [
        "envoy.filters.http.geoip",
        "envoy.geoip_providers.maxmind",
    ],
    deps = [
        "//source/extensions/filters/http/geoip:config",
        "//source/extensions/filters/http/geoip:geoip_filter_lib",
        "//source/extensions/geoip_providers/maxmind:config",
        "//source/extensions/geoip_providers/maxmind:provider_impl",
        "//test/integration:http_integration_lib",
        "//test/test_common:utility_lib",
        "@envoy_api//envoy/extensions/filters/http/geoip/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_mock(
    name = "geoip_mocks",
    hdrs = ["mocks.h"],
    extension_names = ["envoy.filters.http.geoip"],
    deps = ["//envoy/geoip:geoip_provider_driver_interface"],
)

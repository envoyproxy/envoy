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
    name = "config_test",
    size = "small",
    srcs = ["config_test.cc"],
    data = [
        "//test/extensions/geoip_providers/maxmind/test_data:geolocation_databases",
    ],
    extension_names = ["envoy.geoip_providers.maxmind"],
    deps = [
        "//source/extensions/geoip_providers/maxmind:config",
        "//source/extensions/geoip_providers/maxmind:provider_impl",
        "//test/mocks/server:factory_context_mocks",
        "@envoy_api//envoy/extensions/geoip_providers/maxmind/v3:pkg_cc_proto",
    ],
)

envoy_extension_cc_test(
    name = "geoip_provider_test",
    size = "small",
    srcs = ["geoip_provider_test.cc"],
    data = [
        "//test/extensions/geoip_providers/maxmind/test_data:geolocation_databases",
    ],
    extension_names = ["envoy.geoip_providers.maxmind"],
    deps = [
        "//source/extensions/geoip_providers/maxmind:config",
        "//source/extensions/geoip_providers/maxmind:provider_impl",
        "@envoy_api//envoy/extensions/geoip_providers/maxmind/v3:pkg_cc_proto",
    ],
)

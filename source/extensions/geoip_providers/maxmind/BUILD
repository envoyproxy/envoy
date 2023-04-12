load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# HTTP L7 filter that decorates request with geolocation data
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/geoip_filter

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":provider_impl",
        "//envoy/geoip:geoip_provider_driver_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/geoip_providers/common:factory_base_lib",
        "@envoy_api//envoy/extensions/geoip_providers/maxmind/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "provider_impl",
    srcs = ["geoip_provider.cc"],
    hdrs = ["geoip_provider.h"],
    external_deps = ["maxmind"],
    deps = [
        "//envoy/geoip:geoip_provider_driver_interface",
        "@envoy_api//envoy/extensions/filters/http/geoip/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/geoip_providers/maxmind/v3:pkg_cc_proto",
    ],
)

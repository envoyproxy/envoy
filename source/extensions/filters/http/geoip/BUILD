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

envoy_cc_library(
    name = "geoip_filter_lib",
    srcs = ["geoip_filter.cc"],
    hdrs = ["geoip_filter.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/geoip:geoip_provider_driver_interface",
        "//envoy/http:filter_interface",
        "//envoy/runtime:runtime_interface",
        "//source/common/common:assert_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/stats:symbol_table_lib",
        "@envoy_api//envoy/extensions/filters/http/geoip/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/geoip:geoip_provider_driver_interface",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/geoip:geoip_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/geoip/v3:pkg_cc_proto",
    ],
)

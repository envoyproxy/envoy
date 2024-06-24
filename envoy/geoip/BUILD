load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

# HTTP L7 filter that decorates request with geolocation data
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/geoip_filter

envoy_cc_extension(
    name = "geoip_provider_driver_interface",
    hdrs = [
        "geoip_provider_driver.h",
    ],
    tags = ["skip_on_windows"],
    deps = [
        "//envoy/config:typed_config_interface",
        "//envoy/network:address_interface",
        "//envoy/protobuf:message_validator_interface",
        "//envoy/server:factory_context_interface",
        "//source/common/common:hash_lib",
        "//source/common/protobuf:utility_lib",
    ],
)

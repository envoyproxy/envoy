load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Ratelimit L7 HTTP filter
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/rate_limit_filter

envoy_extension_package()

envoy_cc_library(
    name = "ratelimit_lib",
    srcs = ["ratelimit.cc"],
    hdrs = ["ratelimit.h"],
    deps = [
        ":ratelimit_headers_lib",
        "//envoy/http:codes_interface",
        "//envoy/ratelimit:ratelimit_interface",
        "//envoy/stream_info:uint32_accessor_interface",
        "//source/common/common:assert_lib",
        "//source/common/common:empty_string",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/router:config_lib",
        "//source/common/stream_info:uint32_accessor_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:stat_names_lib",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
    ],
)

envoy_cc_library(
    name = "ratelimit_headers_lib",
    srcs = ["ratelimit_headers.cc"],
    hdrs = ["ratelimit_headers.h"],
    deps = [
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/http/common:ratelimit_headers_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":ratelimit_lib",
        "//envoy/registry",
        "//source/common/config:utility_lib",
        "//source/common/protobuf:utility_lib",
        "//source/extensions/filters/common/ratelimit:ratelimit_client_interface",
        "//source/extensions/filters/common/ratelimit:ratelimit_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/ratelimit/v3:pkg_cc_proto",
    ],
)

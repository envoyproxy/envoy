load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter which implements CORS processing (https://en.wikipedia.org/wiki/Cross-origin_resource_sharing)
# Public docs: https://envoyproxy.io/docs/envoy/latest/configuration/http/http_filters/cors_filter

envoy_extension_package()

envoy_cc_library(
    name = "cors_filter_lib",
    srcs = ["cors_filter.cc"],
    hdrs = ["cors_filter.h"],
    external_deps = [
        "abseil_inlined_vector",
    ],
    deps = [
        "//envoy/http:codes_interface",
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//envoy/registry",
        "//envoy/server:filter_config_interface",
        "//source/common/router:config_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/cors:cors_filter_lib",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/cors/v3:pkg_cc_proto",
    ],
)

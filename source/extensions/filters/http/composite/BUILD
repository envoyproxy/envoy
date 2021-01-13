load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter which implements CORS processing (https://en.wikipedia.org/wiki/Cross-origin_resource_sharing)
# Public docs: docs/root/configuration/http_filters/cors_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "filter_lib",
    # srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//include/envoy/http:filter_interface",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "//source/common/http:headers_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/composite:filter_lib",
        "@envoy_api//envoy/extensions/filters/http/composite/v3:pkg_cc_proto",
    ],
)

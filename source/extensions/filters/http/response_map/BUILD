load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

# L7 HTTP filter which implements response_map processing
# Public docs: docs/root/configuration/http_filters/response_map_filter.rst

envoy_package()

envoy_cc_library(
    name = "response_map_filter_lib",
    srcs = ["response_map_filter.cc"],
    hdrs = ["response_map_filter.h"],
    deps = [
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//source/common/response_map:response_map_lib",
        "//source/extensions/filters/http:well_known_names",
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
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//include/envoy/server:filter_config_interface",
        "//source/common/response_map:response_map_lib",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/common:factory_base_lib",
        "//source/extensions/filters/http/response_map:response_map_filter_lib",
        "@envoy_api//envoy/extensions/filters/http/response_map/v3:pkg_cc_proto",
    ],
)

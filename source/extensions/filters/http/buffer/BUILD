load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

# Request buffering and timeout L7 HTTP filter
# Public docs: docs/root/configuration/http_filters/buffer_filter.rst

envoy_extension_package()

envoy_cc_library(
    name = "buffer_filter_lib",
    srcs = ["buffer_filter.cc"],
    hdrs = ["buffer_filter.h"],
    deps = [
        "//include/envoy/event:dispatcher_interface",
        "//include/envoy/http:codes_interface",
        "//include/envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:enum_to_int",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/common/runtime:runtime_lib",
        "//source/extensions/filters/http:well_known_names",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    # Legacy test use. TODO(#9953) clean up.
    visibility = ["//visibility:public"],
    deps = [
        "//include/envoy/registry",
        "//source/extensions/filters/http:well_known_names",
        "//source/extensions/filters/http/buffer:buffer_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/buffer/v3:pkg_cc_proto",
    ],
)

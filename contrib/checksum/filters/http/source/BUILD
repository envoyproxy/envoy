load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# Checksum L7 HTTP filter

envoy_cc_library(
    name = "checksum_filter_lib",
    srcs = ["checksum_filter.cc"],
    hdrs = ["checksum_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:hex_lib",
        "//source/common/common:matchers_lib",
        "//source/common/http:codes_lib",
        "//source/common/http:header_map_lib",
        "//source/common/http:headers_lib",
        "//source/common/http:utility_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/checksum/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":checksum_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/checksum/v3alpha:pkg_cc_proto",
    ],
)

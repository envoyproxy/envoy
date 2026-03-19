load("//bazel:envoy_build_system.bzl", "envoy_contrib_package")
load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# LDAP StartTLS proxy filter

envoy_cc_library(
    name = "filter",
    srcs = [
        "ldap_decoder.cc",
        "ldap_filter.cc",
    ],
    hdrs = [
        "ldap_decoder.h",
        "ldap_filter.h",
        "protocol_helpers.h",
        "protocol_templates.h",
    ],
    repository = "@envoy",
    deps = [
        "//envoy/network:filter_interface",
        "//envoy/server:filter_config_interface",
        "//envoy/stats:stats_interface",
        "//envoy/stats:stats_macros",
        "//source/common/buffer:buffer_lib",
        "//source/common/common:assert_lib",
        "//source/common/common:logger_lib",
        "//source/common/network:filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/ldap_proxy/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    repository = "@envoy",
    deps = [
        ":filter",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/network/ldap_proxy/v3alpha:pkg_cc_proto",
    ],
)

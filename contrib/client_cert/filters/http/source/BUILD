load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_contrib_extension",
    "envoy_cc_library",
    "envoy_contrib_package",
)

licenses(["notice"])  # Apache 2

envoy_contrib_package()

# RFC 9440 Client-Cert forwarding L7 HTTP filter

envoy_cc_library(
    name = "client_cert_filter_lib",
    srcs = ["client_cert_filter.cc"],
    hdrs = ["client_cert_filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//envoy/ssl:connection_interface",
        "//source/common/common:macros",
        "//source/common/http:header_map_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/client_cert/v3alpha:pkg_cc_proto",
    ],
)

envoy_cc_contrib_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":client_cert_filter_lib",
        "//envoy/registry",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//contrib/envoy/extensions/filters/http/client_cert/v3alpha:pkg_cc_proto",
    ],
)

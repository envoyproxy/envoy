load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])

envoy_extension_package()

envoy_cc_library(
    name = "rfc9440_client_cert_filter_lib",
    srcs = ["filter.cc"],
    hdrs = ["filter.h"],
    deps = [
        "//envoy/http:filter_interface",
        "//source/common/http:headers_lib",
        "//source/extensions/filters/http/common:pass_through_filter_lib",
    ],
)

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        ":rfc9440_client_cert_filter_lib",
        "//source/extensions/filters/http/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/http/rfc9440_client_cert/v3:pkg_cc_proto",
    ],
)

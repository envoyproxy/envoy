load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    deps = [
        "//source/common/config:datasource_lib",
        "//source/common/config:utility_lib",
        "//source/extensions/certificate_providers:certificate_provider_lib",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/certificate_providers/static_cert_provider/v3:pkg_cc_proto",
    ],
)

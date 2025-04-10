load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = [
        "mtls_authenticated.cc",
    ],
    hdrs = [
        "mtls_authenticated.h",
    ],
    deps = [
        "//source/common/tls/cert_validator:cert_validator_lib",
        "//source/extensions/filters/common/rbac:matchers_interface",
        "@envoy_api//envoy/extensions/rbac/principals/mtls_authenticated/v3:pkg_cc_proto",
    ],
)

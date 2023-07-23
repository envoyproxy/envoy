load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "config",
    srcs = ["getaddrinfo.cc"],
    hdrs = ["getaddrinfo.h"],
    deps = [
        "//envoy/network:dns_resolver_interface",
        "//envoy/registry",
        "@envoy_api//envoy/extensions/network/dns_resolver/getaddrinfo/v3:pkg_cc_proto",
    ],
)

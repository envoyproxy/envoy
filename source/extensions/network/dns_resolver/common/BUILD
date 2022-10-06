load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "res_query",
    srcs = ["res_query.cc"],
    hdrs = ["res_query.h"],
    deps = [
        "//envoy/network:dns_resolver_interface",
        "//envoy/registry",
    ],
)

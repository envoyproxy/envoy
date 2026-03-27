load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_cc_library",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_library(
    name = "hickory_dns_lib",
    srcs = ["hickory_dns_impl.cc"],
    hdrs = ["hickory_dns_impl.h"],
    deps = [
        "//envoy/event:dispatcher_interface",
        "//envoy/network:dns_interface",
        "//envoy/network:dns_resolver_interface",
        "//envoy/registry",
        "//source/common/network:utility_lib",
        "//source/extensions/dynamic_modules:dynamic_modules_lib",
        "@envoy_api//envoy/extensions/network/dns_resolver/hickory/v3:pkg_cc_proto",
    ],
)

envoy_cc_extension(
    name = "config",
    deps = [
        ":hickory_dns_lib",
        "//source/extensions/dynamic_modules/builtin_extensions:hickory_dns_static",
    ],
)

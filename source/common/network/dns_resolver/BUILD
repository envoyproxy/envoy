load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

licenses(["notice"])  # Apache 2

envoy_package()

envoy_cc_library(
    name = "dns_factory_util_lib",
    srcs = ["dns_factory_util.cc"],
    hdrs = ["dns_factory_util.h"],
    deps = [
        "//envoy/network:dns_resolver_interface",
        "@envoy_api//envoy/config/bootstrap/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/common/dynamic_forward_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/udp/dns_filter/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/dns_resolver/apple/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/network/dns_resolver/cares/v3:pkg_cc_proto",
    ],
)

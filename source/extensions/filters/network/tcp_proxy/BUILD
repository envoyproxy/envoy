licenses(["notice"])  # Apache 2

# TCP proxy L4 network filter.
# Public docs: docs/root/configuration/network_filters/tcp_proxy_filter.rst

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "config",
    srcs = ["config.cc"],
    hdrs = ["config.h"],
    security_posture = "robust_to_untrusted_downstream",
    deps = [
        "//include/envoy/registry",
        "//source/common/tcp_proxy",
        "//source/extensions/filters/network:well_known_names",
        "//source/extensions/filters/network/common:factory_base_lib",
        "@envoy_api//envoy/extensions/filters/network/tcp_proxy/v3:pkg_cc_proto",
    ],
)

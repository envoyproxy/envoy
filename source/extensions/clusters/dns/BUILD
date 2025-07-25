load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "dns_cluster_lib",
    srcs = ["dns_cluster.cc"],
    hdrs = ["dns_cluster.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/upstream:cluster_factory_includes",
        "//source/common/upstream:upstream_includes",
        "//source/extensions/clusters/logical_dns:logical_dns_cluster_lib",
        "//source/extensions/clusters/strict_dns:strict_dns_cluster_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dns/v3:pkg_cc_proto",
    ],
)

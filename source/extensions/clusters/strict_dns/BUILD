load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "strict_dns_cluster_lib",
    srcs = ["strict_dns_cluster.cc"],
    hdrs = ["strict_dns_cluster.h"],
    # prevously considered core code.
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/upstream:cluster_factory_includes",
        "//source/common/upstream:upstream_includes",
        "//source/extensions/clusters/common:dns_cluster_backcompat_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dns/v3:pkg_cc_proto",
    ],
)

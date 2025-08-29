load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "cluster",
    srcs = ["dns_srv_cluster.cc"],
    hdrs = ["dns_srv_cluster.h"],
    # prevously considered core code.
    visibility = ["//visibility:public"],
    deps = [
        "//envoy/upstream:cluster_factory_interface",
        "//source/common/common:dns_utils_lib",
        "//source/common/common:empty_string",
        "//source/common/config:utility_lib",
        "//source/common/network:address_lib",
        "//source/common/network:utility_lib",
        "//source/common/protobuf",
        "//source/common/protobuf:utility_lib",
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/dns_srv/v3:pkg_cc_proto",
    ],
)

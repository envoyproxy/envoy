load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "static_cluster_lib",
    srcs = ["static_cluster.cc"],
    hdrs = ["static_cluster.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//source/common/upstream:cluster_factory_includes",
        "//source/common/upstream:upstream_includes",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)

licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_library",
    "envoy_package",
)

envoy_package()

envoy_cc_library(
    name = "cluster_util",
    srcs = ["cluster_util.cc"],
    hdrs = ["cluster_util.h"],
    deps = [
        "//source/common/upstream:upstream_includes",
    ],
)

envoy_cc_library(
    name = "cluster_lb",
    srcs = ["cluster_lb.cc"],
    hdrs = ["cluster_lb.h"],
    deps = [
        ":cluster_util",
        "//source/common/upstream:upstream_includes",
    ],
)

envoy_cc_library(
    name = "cluster",
    srcs = ["cluster.cc"],
    hdrs = ["cluster.h"],
    deps = [
        ":cluster_lb",
        "//source/common/upstream:cluster_factory_lib",
        "//source/extensions/clusters:well_known_names",
        "@envoy_api//envoy/config/cluster/aggregate:cluster_cc",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "cluster",
    srcs = ["cluster.cc"],
    hdrs = [
        "cluster.h",
        "lb_context.h",
    ],
    deps = [
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/extensions/load_balancing_policies/common:load_balancer_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
    ],
)

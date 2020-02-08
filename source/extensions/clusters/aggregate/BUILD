licenses(["notice"])  # Apache 2

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_package",
)

envoy_package()

envoy_cc_extension(
    name = "cluster",
    srcs = ["cluster.cc"],
    hdrs = [
        "cluster.h",
        "lb_context.h",
    ],
    security_posture = "requires_trusted_downstream_and_upstream",
    deps = [
        "//source/common/upstream:cluster_factory_lib",
        "//source/common/upstream:upstream_includes",
        "//source/extensions/clusters:well_known_names",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
    ],
)

load(
    "//bazel:envoy_build_system.bzl",
    "envoy_cc_extension",
    "envoy_extension_package",
)

licenses(["notice"])  # Apache 2

envoy_extension_package()

envoy_cc_extension(
    name = "load_balancer_lib",
    srcs = ["load_balancer.cc"],
    hdrs = ["load_balancer.h"],
    deps = [
        "//source/common/common:minimal_logger_lib",
        "//source/common/upstream:lb_ring_lib",
        "//source/common/upstream:load_balancer_lib",
        "//source/common/upstream:upstream_lib",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/deterministic_aperture/v3:pkg_cc_proto",
    ],
)
